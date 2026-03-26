# Qubic Core Lite — Claude Code Guide

## Project Overview
Lightweight Qubic blockchain node that runs on Linux/Windows (no UEFI required). Implements consensus, smart contracts (QPI), solution scoring, networking, and an RPC API (Linux only).

## Build

**Requirements:** Clang 18+ (AppleClang 15+ on macOS), CMake 3.15+, NASM 2.16+

```bash
# Configure
cmake -B build \
  -D CMAKE_C_COMPILER=clang \
  -D CMAKE_CXX_COMPILER=clang++ \
  -D BUILD_TESTS=ON \
  -D BUILD_BINARY=OFF \
  -D CMAKE_BUILD_TYPE=Release \
  -D ENABLE_AVX512=OFF

# Build
cmake --build build -- -j$(nproc)
```

Key CMake options:
- `BUILD_TESTS` — Google Test suite
- `BUILD_BINARY` — EFI application
- `BUILD_BENCHMARK` — UEFI benchmark
- `USE_SANITIZER` — ASAN/UBSAN (test builds)

## Run Tests

```bash
cd build && ctest --output-on-failure
# or directly:
./build/bin/qubic_core_tests
```

Test sources live in `test/`. Most test files are disabled by default in `test/CMakeLists.txt` — enable them individually.

## Key Directories

| Path | Purpose |
|------|---------|
| `src/` | Main source (entry: `src/qubic.cpp`) |
| `src/contracts/` | Smart contracts (Qbay, QBond, Qearn, QubicSolanaBridge, …) |
| `src/contract_core/` | Contract execution engine & QPI definitions |
| `src/network_core/` | Networking |
| `src/network_messages/` | Wire protocol message types |
| `lib/platform_os/` | Linux/Windows platform layer |
| `lib/platform_common/` | Cross-platform utilities |
| `test/` | Google Tests + test data CSVs |
| `tools/` | Utilities (score test generator, Python scripts, testnet scripts) |
| `docker/` | Docker environments for local testnets |

## Configuration

- `src/private_settings.h` — runtime config: seeds, ports, feature flags
- `src/public_settings.h` — compile-time constants

Important feature flags (in `private_settings.h`):
- `#define TESTNET` — enable testnet mode
- `#define USE_SWAP` — disk-as-RAM for mainnet
- `#define INCLUDE_CONTRACT_TEST_EXAMPLES` — enable test contracts
- `#define TICK_STORAGE_AUTOSAVE_MODE` — tick snapshots

## Coding Conventions

- **C++20**, Clang only (no MSVC-isms in core logic)
- No STL in hot paths — platform primitives preferred
- Smart contracts implement QPI interface defined in `src/contract_core/`
- New contracts go in `src/contracts/` with a corresponding test in `test/`
- Test data (ground-truth CSVs) lives in `test/data/`

## Local Testnet (Docker)

### Start

```bash
docker compose -f docker/testnet/docker-compose.yml up --build
```

### Stop

```bash
docker compose -f docker/testnet/docker-compose.yml down
```

The node boots at **epoch 204**, ticks every ~3s (`TICKING_DELAY=3000`), and exposes:
- `localhost:41841` — HTTP RPC API
- `localhost:31841` — P2P

A **Bob-compatible indexer** runs alongside the node — same REST API as the production Bob indexer, backed by SQLite:
- `localhost:3002` — Indexer REST API (SQLite-backed, Bob-compatible endpoints)

Broadcast transactions via the indexer proxy (`POST /broadcastTransaction`) so it can capture tx data before the node's circular buffer evicts it.

#### Indexer endpoints (Bob-compatible)

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/status` | Sync status — Bob format |
| `GET` | `/tx/{hash}` | Single transaction by hash — Bob format |
| `GET` | `/balance/{identity}` | Account balance (proxied to node) |
| `GET` | `/tick/{tick_number}` | Tick data + transactions |
| `POST` | `/broadcastTransaction` | Proxy: captures tx data, forwards to node. Accepts `{encodedTransaction: "base64"}` or `{data: "hex"}` |
| `POST` | `/getQuTransfersForIdentity` | QU transfers for identity in tick range — Bob format |
| `POST` | `/querySmartContract` | Query contract (proxied to node) |
| `POST` | `/findLog` | Stub — returns `[]` (no log indexing on testnet) |
| `GET` | `/ticks/latest?limit=N` | Most recent ticks (convenience, not in real Bob) |

> `/broadcast-transaction` is also accepted as an alias for backward compatibility.

#### Response formats

**`GET /tx/{hash}`:**
```json
{
  "hash": "czz...",
  "tick": 46310130,
  "source": "ODPC...",
  "destination": "ABAA...",
  "amount": 1000,
  "inputType": 1,
  "inputSize": 88
}
```

**`POST /getQuTransfersForIdentity`** (body: `{"fromTick": 0, "toTick": 99999999, "identity": "..."}`):
```json
[
  {
    "hash": "czz...",
    "tick": 46310130,
    "source": "ODPC...",
    "destination": "ABAA...",
    "amount": 1000,
    "inputType": 1
  }
]
```

**`GET /status`:**
```json
{
  "currentProcessingEpoch": 204,
  "currentFetchingTick": 46310130,
  "currentIndexingTick": 46310128,
  "initialTick": 46310000,
  "bobVersion": "1.0.0-testnet"
}
```

### Verify it's running

```bash
curl http://localhost:41841/live/v1/tick-info   # node
curl http://localhost:3001/status               # indexer
```

### Testnet build flags (in `src/qubic.cpp`)

```cpp
#define TESTNET
#define TESTNET_PREFILL_QUS  // sends test QUs to computors at epoch start
// #define USE_SWAP           // disabled — no disk-as-RAM needed
```

### Adding a contract to the testnet

Contracts are compiled into the binary — there is no runtime deployment. To activate a contract:

1. Ensure the contract header exists in `src/contracts/`
2. Add `#define` + `#include` block in `src/contract_core/contract_def.h`
3. Add an entry to `contractDescriptions[]` with `constructionEpoch` matching the boot epoch (204)
4. Add `REGISTER_CONTRACT_FUNCTIONS_AND_PROCEDURES(NAME)` to `initializeContracts()`
5. Rebuild: `docker compose -f docker/testnet/docker-compose.yml up --build`

### Querying a contract via RPC

```bash
curl http://localhost:41841/live/v1/querySmartContract \
  -H 'Content-Type: application/json' \
  -d '{"contractIndex": <index>, "inputType": <functionId>, "inputHex": ""}'
```

### QubicSolanaBridge (QSB) — contract index 26

| Function | `inputType` | Description |
|---|---|---|
| `GetConfig` | 1 | Admin, thresholds, fee params, paused state |
| `IsOracle` | 2 | Check if address is an oracle |
| `IsPauser` | 3 | Check if address is a pauser |
| `GetLockedOrder` | 4 | Fetch a specific locked order |
| `IsOrderFilled` | 5 | Check if an order is filled |
| `ComputeOrderHash` | 6 | Compute hash for an order |
| `GetOracles` | 7 | List all oracle addresses |
| `GetPausers` | 8 | List all pauser addresses |
| `GetLockedOrders` | 9 | Paginated active locked orders |
| `GetFilledOrders` | 10 | Paginated filled order history |

QSB **procedures** (require a signed transaction with `invocationReward >= amount`):

| Procedure | `inputType` | Payload size | Notes |
|---|---|---|---|
| `Lock` | 1 | 88 bytes | Bridge QU → Solana |
| `OverrideLock` | 2 | — | Oracle only |
| `Unlock` | 3 | — | Oracle only |
| `TransferAdmin` | 10 | — | Admin only |
| `EditOracleThreshold` | 11 | — | Admin only |
| `AddRole` | 12 | — | Admin only |
| `RemoveRole` | 13 | — | Admin only |
| `Pause` | 14 | — | Pauser only |
| `Unpause` | 15 | — | Pauser only |
| `EditFeeParameters` | 16 | — | Admin only |

**Lock payload layout (88 bytes, all little-endian):**
```
[0..7]   uint64  amount
[8..15]  uint64  relayerFee
[16..79] uint8[64] toAddress  (zero-padded ASCII)
[80..83] uint32  networkOut   (1 = Solana mainnet)
[84..87] uint32  nonce        (random)
```

**Admin identity (testnet):** initialized from `id(100, 200, 300, 400)` in `INITIALIZE()`.

### Testnet scripts (`tools/testnet-scripts/`)

Node.js scripts for interacting with the testnet. Require `@qubic-lib/qubic-ts-library@0.1.6`.

```bash
cd tools/testnet-scripts
npm install
node lock.js [amount] [relayerFee]   # Build + sign + broadcast a QSB Lock tx
```

The funded test seed is hardcoded in `lock.js` (matches the seed in `src/private_settings.h`).
Transactions are broadcast via the indexer proxy (`POST http://localhost:3001/broadcastTransaction`) which captures full tx data before forwarding to the node. After broadcast, `lock.js` polls `http://localhost:3001/tx/{hash}` for confirmation.

### Important field names

```bash
# Indexer: get transaction by hash (GET)
curl http://localhost:3002/tx/ABCDEF...

# Indexer: get QU transfers for identity in a tick range
curl -X POST http://localhost:3002/getQuTransfersForIdentity \
  -H 'Content-Type: application/json' \
  -d '{"fromTick": 46310000, "toTick": 46320000, "identity": "ABCDEF..."}'

# Node: get transaction by hash — field is "txHash"
curl http://localhost:41841/getTransactionByHash \
  -H 'Content-Type: application/json' \
  -d '{"txHash": "ABCDEF..."}'

# Node: get transactions for a tick — field is "tick"
curl http://localhost:41841/getTransactionsForTick \
  -H 'Content-Type: application/json' \
  -d '{"tick": 46310050}'
```

### Contract destination address encoding

A contract's destination public key is its index stored as a **little-endian uint64** in the first 8 bytes of a 32-byte array (remaining bytes zero). QSB is index 26.

```javascript
function contractDestination(index) {
    const buf = new Uint8Array(32);
    let v = BigInt(index);
    for (let i = 0; i < 8; i++) { buf[i] = Number(v & 0xffn); v >>= 8n; }
    return buf;
}
```
