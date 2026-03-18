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
| `tools/` | Utilities (score test generator, Python scripts) |
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

The node boots at **epoch 204**, ticks every ~1s, and exposes:
- `localhost:41841` — HTTP RPC API
- `localhost:31841` — P2P

### Verify it's running

```bash
curl http://localhost:41841/live/v1/tick-info
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

### QubicSolanaBridge (QSB) — contract index 25

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

