/**
 * QSB Lock procedure — testnet script
 *
 * Builds, signs, and broadcasts a Lock transaction to the QubicSolanaBridge
 * contract on the local testnet, then polls until the transaction appears.
 *
 * Usage:
 *   node lock.js [amount] [relayerFee]
 *
 * Defaults: amount=1000, relayerFee=10
 */

'use strict';

const qubicLib = require('@qubic-lib/qubic-ts-library');
const { QubicTransaction, QubicHelper, DynamicPayload, PublicKey, Long } = qubicLib.default ?? qubicLib;

const RPC_URL        = 'http://localhost:41841';
const BOB_URL        = 'http://localhost:3001';
const SEED           = 'qubicorelitebyfeiyuivqubicqubicqubicqubicqubicquicqubic'; // funded custom seed
const CONTRACT_INDEX = 26;  // QSB index
const INPUT_TYPE     = 1;   // Lock procedure
const TICK_OFFSET    = 5;   // broadcast target = currentTick + TICK_OFFSET (min 2 per TICK_TRANSACTIONS_PUBLICATION_OFFSET)

// ── helpers ──────────────────────────────────────────────────────────────────

async function getCurrentTick() {
    const res = await fetch(`${RPC_URL}/live/v1/tick-info`);
    if (!res.ok) throw new Error(`tick-info HTTP ${res.status}`);
    const body = await res.json();
    return body.tick;
}

/** Convert contract index to its 32-byte "address" (index as little-endian u64, rest zeros) */
function contractDestination(index) {
    const buf = new Uint8Array(32);
    // write index as little-endian uint64 into first 8 bytes
    let v = BigInt(index);
    for (let i = 0; i < 8; i++) {
        buf[i] = Number(v & 0xffn);
        v >>= 8n;
    }
    return buf;
}

/** Encode Lock_input as little-endian binary */
function buildLockPayload(amount, relayerFee, toAddress, networkOut, nonce) {
    // Lock_input layout:
    //   uint64  amount      (8 bytes)
    //   uint64  relayerFee  (8 bytes)
    //   uint8   toAddress[64] (64 bytes)
    //   uint32  networkOut  (4 bytes)
    //   uint32  nonce       (4 bytes)
    // Total: 88 bytes
    const buf = new ArrayBuffer(88);
    const view = new DataView(buf);
    view.setBigUint64(0,  BigInt(amount),     true); // little-endian
    view.setBigUint64(8,  BigInt(relayerFee), true);
    // toAddress: zero-padded 64 bytes (ASCII encode the string)
    const addrBytes = new TextEncoder().encode(toAddress.slice(0, 64));
    new Uint8Array(buf, 16, addrBytes.length).set(addrBytes);
    view.setUint32(80, networkOut, true);
    view.setUint32(84, nonce, true);
    return new Uint8Array(buf);
}

// ── main ─────────────────────────────────────────────────────────────────────

(async () => {
    const amount     = parseInt(process.argv[2] ?? '1000');
    const relayerFee = parseInt(process.argv[3] ?? '10');

    console.log(`\n=== QSB Lock Procedure (testnet) ===`);
    console.log(`  Amount    : ${amount} QU`);
    console.log(`  RelayerFee: ${relayerFee} QU`);

    // 1. Derive public identity from seed
    const helper = new QubicHelper();
    const { publicKey, publicId } = await helper.createIdPackage(SEED);
    console.log(`\nCaller identity : ${publicId}`);

    // 2. Get current tick
    const tick = await getCurrentTick();
    const targetTick = tick + TICK_OFFSET;
    console.log(`Current tick    : ${tick} → targeting tick ${targetTick}`);

    // 3. Check balance
    const balRes = await fetch(`${RPC_URL}/live/v1/balances/${publicId}`);
    if (balRes.ok) {
        const balBody = await balRes.json();
        console.log(`Balance         : ${balBody.balance?.balance ?? 'unknown'} QU`);
    }

    // 4. Build transaction
    const destBytes = contractDestination(CONTRACT_INDEX);
    const dest = new PublicKey(destBytes);

    const payloadBytes = buildLockPayload(
        amount,
        relayerFee,
        'SolanaDummyAddressForTestingPurposesOnly123456789012', // 50-char dummy
        1,   // networkOut = Solana mainnet
        Math.floor(Math.random() * 0xffffffff), // random nonce
    );

    const payload = new DynamicPayload(payloadBytes.length);
    payload.setPayload(payloadBytes);

    const tx = new QubicTransaction()
        .setSourcePublicKey(new PublicKey(publicKey))
        .setDestinationPublicKey(dest)
        .setAmount(new Long(amount))
        .setTick(targetTick)
        .setInputType(INPUT_TYPE)
        .setInputSize(payloadBytes.length)
        .setPayload(payload);

    const builtTx  = await tx.build(SEED);
    const hexData  = Buffer.from(builtTx).toString('hex');
    const txId     = tx.getId();

    console.log(`\nTransaction ID  : ${txId}`);
    console.log(`Payload size    : ${payloadBytes.length} bytes`);

    // 5. Broadcast via Bob
    console.log(`\nBroadcasting via Bob...`);
    const broadcastRes = await fetch(`${BOB_URL}/broadcastTransaction`, {
        method : 'POST',
        headers: { 'Content-Type': 'application/json' },
        body   : JSON.stringify({ data: hexData }),
    });

    const broadcastBody = await broadcastRes.json();
    if (!broadcastRes.ok) {
        console.error('Broadcast failed:', JSON.stringify(broadcastBody, null, 2));
        process.exit(1);
    }
    console.log('Broadcast response:', JSON.stringify(broadcastBody, null, 2));

    // 6. Poll Bob for the transaction to appear (up to 60s)
    console.log(`\nWaiting for tx to be indexed by Bob...`);
    const deadline = Date.now() + 60_000;
    let found = false;
    while (Date.now() < deadline) {
        await new Promise(r => setTimeout(r, 3000));
        try {
            const curTick = await getCurrentTick();
            if (curTick < targetTick) {
                process.stdout.write(`\rWaiting... (current tick: ${curTick}, target: ${targetTick})  `);
                continue;
            }
            const txRes = await fetch(`${BOB_URL}/tx/${txId}`);
            if (txRes.ok) {
                const txBody = await txRes.json();
                if (txBody && txBody.hash) {
                    console.log('\nTransaction found:');
                    console.log(JSON.stringify(txBody, null, 2));
                    found = true;
                    break;
                }
            }
        } catch (_) { /* not yet indexed */ }
    }

    if (!found) {
        console.log('\nTx not indexed within 60s — it may still be pending or the tick was missed.');
        console.log(`Verify manually:`);
        console.log(`  curl ${BOB_URL}/tx/${txId}`);
    }

    // 7. Query QSB state to confirm lock was recorded
    console.log('\n--- QSB GetLockedOrders (inputType=9) ---');
    const qsbRes = await fetch(`${RPC_URL}/live/v1/querySmartContract`, {
        method : 'POST',
        headers: { 'Content-Type': 'application/json' },
        body   : JSON.stringify({ contractIndex: CONTRACT_INDEX, inputType: 9, inputHex: '0000000000000000' }),
    });
    const qsbBody = await qsbRes.json();
    console.log(JSON.stringify(qsbBody, null, 2));
})().catch(err => {
    console.error('Error:', err.message ?? err);
    process.exit(1);
});
