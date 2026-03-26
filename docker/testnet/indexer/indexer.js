'use strict';

const Database  = require('better-sqlite3');
const express   = require('express');
const qubicLib  = require('@qubic-lib/qubic-ts-library');
const { QubicHelper } = qubicLib.default ?? qubicLib;
const helper    = new QubicHelper();

// ── Config ────────────────────────────────────────────────────────────────────
const NODE_URL  = process.env.NODE_URL  || 'http://qubic-testnet:41841';
const DB_PATH   = process.env.DB_PATH   || '/data/indexer.db';
const HTTP_PORT = parseInt(process.env.HTTP_PORT || '3001');
const POLL_MS   = parseInt(process.env.POLL_MS   || '500');
const VERSION   = '1.0.0-testnet';

// ── Transaction binary decoder ────────────────────────────────────────────────
// Transaction struct layout (80 bytes header):
//   sourcePublicKey      [0..31]  32 bytes
//   destinationPublicKey [32..63] 32 bytes
//   amount               [64..71] int64  little-endian
//   tick                 [72..75] uint32 little-endian
//   inputType            [76..77] uint16 little-endian
//   inputSize            [78..79] uint16 little-endian
function decodeTxBinary(buf) {
    const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
    return {
        sourcePubKey: buf.slice(0, 32),
        destPubKey:   buf.slice(32, 64),
        amount:       Number(view.getBigInt64(64, true)),
        tick:         view.getUint32(72, true),
        inputType:    view.getUint16(76, true),
        inputSize:    view.getUint16(78, true),
    };
}

/** Convert 32-byte public key to Qubic identity string (uses K12 checksum). */
async function pubKeyToIdentity(bytes) {
    try { return await helper.getIdentity(bytes); } catch { return null; }
}

// ── Database ──────────────────────────────────────────────────────────────────
const db = new Database(DB_PATH);
db.pragma('journal_mode = WAL');
db.pragma('synchronous = NORMAL');

db.exec(`
  CREATE TABLE IF NOT EXISTS ticks (
    tick           INTEGER PRIMARY KEY,
    epoch          INTEGER,
    timestamp      TEXT,
    computor_index INTEGER,
    tx_count       INTEGER DEFAULT 0,
    raw            TEXT
  );

  CREATE TABLE IF NOT EXISTS transactions (
    hash        TEXT PRIMARY KEY,
    tick        INTEGER,
    source      TEXT,
    destination TEXT,
    amount      INTEGER,
    input_type  INTEGER,
    input_size  INTEGER
  );

  CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT
  );

  CREATE INDEX IF NOT EXISTS idx_tx_tick   ON transactions(tick);
  CREATE INDEX IF NOT EXISTS idx_tx_source ON transactions(source);
  CREATE INDEX IF NOT EXISTS idx_tx_dest   ON transactions(destination);
`);

const getMeta       = db.prepare('SELECT value FROM meta WHERE key = ?');
const setMeta       = db.prepare('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)');
const insertTxStmt  = db.prepare(`
    INSERT OR IGNORE INTO transactions (hash, tick, source, destination, amount, input_type, input_size)
    VALUES (?, ?, ?, ?, ?, ?, ?)
`);
const patchTxStmt   = db.prepare(`
    UPDATE transactions
    SET source = ?, destination = ?, amount = ?, input_type = ?, input_size = ?
    WHERE hash = ? AND source IS NULL
`);

function getLastIndexedTick() {
    const row = getMeta.get('last_tick');
    return row ? parseInt(row.value) : 0;
}

function getInitialTick() {
    const row = getMeta.get('initial_tick');
    return row ? parseInt(row.value) : 0;
}

function getIndexedEpoch() {
    const row = db.prepare('SELECT epoch FROM ticks ORDER BY tick DESC LIMIT 1').get();
    return row?.epoch ?? 0;
}

// ── Node helpers ──────────────────────────────────────────────────────────────
async function rpcGet(path) {
    const res = await fetch(`${NODE_URL}${path}`);
    if (!res.ok) throw new Error(`GET ${path} → HTTP ${res.status}`);
    return res.json();
}

async function rpcPost(path, body) {
    const res = await fetch(`${NODE_URL}${path}`, {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify(body),
    });
    if (!res.ok) throw new Error(`POST ${path} → HTTP ${res.status}`);
    return res.json();
}

async function getLastProcessedTick() {
    const data = await rpcGet('/getLastProcessedTick');
    return { tickNumber: data.tickNumber, epoch: data.epoch, intervalInitialTick: data.intervalInitialTick };
}

// ── Tick indexer ──────────────────────────────────────────────────────────────
async function indexTick(tickNumber) {
    let tickData;
    try {
        tickData = await rpcPost('/getTickData', { tickNumber });
    } catch { return; }

    const digests = tickData.transactionDigests || [];
    let txList = [];
    try {
        const r = await rpcPost('/getTransactionsForTick', { tickNumber });
        txList = r.transactions || [];
    } catch { /* aged out — use digests only */ }

    const txMap = {};
    for (const entry of txList) {
        const tx = entry.transaction || entry;
        if (tx?.txId) txMap[tx.txId.toLowerCase()] = tx;
    }

    db.prepare(`INSERT OR IGNORE INTO ticks (tick, epoch, timestamp, computor_index, tx_count, raw)
                VALUES (?, ?, ?, ?, ?, ?)`)
      .run(tickNumber, tickData.epoch ?? null, tickData.timestamp ?? null,
           tickData.computorIndex ?? null, digests.length, JSON.stringify(tickData));

    for (const digest of digests) {
        const hash = digest.toLowerCase();
        const tx   = txMap[hash];
        // Insert full row if we have the data, or a stub if not
        insertTxStmt.run(
            hash, tickNumber,
            tx?.sourceId ?? null, tx?.destId ?? null,
            tx?.amount   ?? null, tx?.inputType ?? null, tx?.inputSize ?? null,
        );
        // Patch any broadcast-intercepted stub that has NULL source/dest
        if (tx?.sourceId) {
            patchTxStmt.run(tx.sourceId, tx.destId, tx.amount, tx.inputType, tx.inputSize, hash);
        }
    }
}

// ── Indexer loop ──────────────────────────────────────────────────────────────
let indexerRunning = false;

async function poll() {
    if (indexerRunning) return;
    indexerRunning = true;
    try {
        const { tickNumber: lastProcessed, epoch, intervalInitialTick } = await getLastProcessedTick();
        const lastIndexed   = getLastIndexedTick();

        // Detect node restart: if the node's last processed tick is behind our cursor,
        // the node must have restarted (tick counters only go up). Reset to epoch start.
        if (lastProcessed < lastIndexed && intervalInitialTick) {
            const resetTo = intervalInitialTick - 1;
            console.log(`[indexer] node restart detected (epoch ${epoch}, node at ${lastProcessed}, we were at ${lastIndexed}), resetting cursor to ${resetTo}`);
            setMeta.run('last_tick',    String(resetTo));
            setMeta.run('initial_tick', String(intervalInitialTick));
            return;
        }

        if (lastProcessed <= lastIndexed) return;

        const lag   = lastProcessed - lastIndexed;
        const batch = lag > 200 ? 50 : 1;
        const from  = lastIndexed + 1;
        const to    = Math.min(lastProcessed, from + batch - 1);

        for (let tick = from; tick <= to; tick++) await indexTick(tick);

        setMeta.run('last_tick', String(to));
        if (to > from) console.log(`[indexer] ticks ${from}–${to} indexed (latest: ${lastProcessed})`);
        else           console.log(`[indexer] tick ${to} indexed`);
    } catch (err) {
        if (!err.message?.includes('ECONNREFUSED') && !err.message?.includes('ENOTFOUND')) {
            console.error('[indexer] poll error:', err.message);
        }
    } finally {
        indexerRunning = false;
    }
}

// ── HTTP API (Bob-compatible) ─────────────────────────────────────────────────
const app = express();
app.use(express.json());

function txRow(row) {
    return {
        hash:        row.hash,
        tick:        row.tick,
        source:      row.source,
        destination: row.destination,
        amount:      row.amount,
        inputType:   row.input_type,
        inputSize:   row.input_size,
    };
}

// GET /status — Bob format
app.get('/status', (_req, res) => {
    const lastIndexed = getLastIndexedTick();
    res.json({
        currentProcessingEpoch:   getIndexedEpoch(),
        currentFetchingTick:      lastIndexed,
        currentFetchingLogTick:   lastIndexed,
        currentVerifyLoggingTick: lastIndexed,
        currentIndexingTick:      lastIndexed,
        initialTick:              getInitialTick(),
        bobVersion:               VERSION,
        bobVersionGitHash:        'testnet',
        bobCompiler:              'Node.js',
    });
});

// GET /tx/:hash — Bob format
app.get('/tx/:hash', (req, res) => {
    const row = db.prepare('SELECT * FROM transactions WHERE hash = ?').get(req.params.hash.toLowerCase());
    if (!row) return res.status(404).json({ ok: false, error: 'Transaction not found' });
    res.json(txRow(row));
});

// GET /balance/:identity — proxy to node
app.get('/balance/:identity', async (req, res) => {
    try {
        const data = await rpcGet(`/live/v1/balances/${req.params.identity}`);
        res.json(data);
    } catch (err) {
        res.status(500).json({ ok: false, error: err.message });
    }
});

// GET /tick/:n — Bob format
app.get('/tick/:n', (req, res) => {
    const tick = parseInt(req.params.n);
    const row  = db.prepare('SELECT * FROM ticks WHERE tick = ?').get(tick);
    if (!row) return res.status(404).json({ ok: false, error: 'Tick not found' });
    const txs  = db.prepare('SELECT * FROM transactions WHERE tick = ?').all(tick);
    res.json({
        tick:          row.tick,
        epoch:         row.epoch,
        timestamp:     row.timestamp,
        computorIndex: row.computor_index,
        txCount:       row.tx_count,
        transactions:  txs.map(txRow),
    });
});

// POST /broadcastTransaction — Bob format
// Accepts:  { data: "hex..." }          (Bob native)
//       or  { encodedTransaction: "b64..." }  (legacy compat)
// Also aliased as POST /broadcast-transaction for backward compatibility.
async function handleBroadcast(req, res) {
    const { data, encodedTransaction } = req.body || {};

    let base64Encoded, decoded;
    if (data) {
        const hex = data.startsWith('0x') ? data.slice(2) : data;
        const buf = Buffer.from(hex, 'hex');
        base64Encoded = buf.toString('base64');
        try { decoded = decodeTxBinary(buf); } catch { /* ignore */ }
    } else if (encodedTransaction) {
        base64Encoded = encodedTransaction;
        try { decoded = decodeTxBinary(Buffer.from(encodedTransaction, 'base64')); } catch { /* ignore */ }
    } else {
        return res.status(400).json({ ok: false, error: 'Missing data or encodedTransaction' });
    }

    let nodeResponse;
    try {
        const r = await fetch(`${NODE_URL}/live/v1/broadcast-transaction`, {
            method:  'POST',
            headers: { 'Content-Type': 'application/json' },
            body:    JSON.stringify({ encodedTransaction: base64Encoded }),
        });
        nodeResponse = await r.json();
    } catch (err) {
        return res.status(502).json({ ok: false, error: `Node unreachable: ${err.message}` });
    }

    const hash = (nodeResponse.transactionId || '').toLowerCase();
    if (hash && decoded) {
        // Decode source/dest identities from binary using K12 checksum
        const [sourceId, destId] = await Promise.all([
            pubKeyToIdentity(decoded.sourcePubKey),
            pubKeyToIdentity(decoded.destPubKey),
        ]);
        insertTxStmt.run(hash, decoded.tick, sourceId, destId, decoded.amount, decoded.inputType, decoded.inputSize);
        console.log(`[indexer] captured tx ${hash} (tick ${decoded.tick}, inputType ${decoded.inputType}, amount ${decoded.amount}, source ${sourceId})`);
    }

    res.json(nodeResponse);
}

app.post('/broadcastTransaction',   handleBroadcast);
app.post('/broadcast-transaction',  handleBroadcast); // backward compat

// POST /getQuTransfersForIdentity — Bob format
app.post('/getQuTransfersForIdentity', (req, res) => {
    const { fromTick, toTick, identity } = req.body || {};
    if (!identity || fromTick === undefined || toTick === undefined) {
        return res.status(400).json({ ok: false, error: 'Missing fromTick, toTick, or identity' });
    }
    const id   = identity.toUpperCase();
    const rows = db.prepare(`
        SELECT * FROM transactions
        WHERE (source = ? OR destination = ?) AND tick >= ? AND tick <= ?
        ORDER BY tick DESC
    `).all(id, id, fromTick, toTick);
    res.json(rows.map(t => ({
        hash:        t.hash,
        tick:        t.tick,
        source:      t.source,
        destination: t.destination,
        amount:      t.amount,
        inputType:   t.input_type,
    })));
});

// POST /getAssetTransfersForIdentity — stub (no asset indexing on testnet)
app.post('/getAssetTransfersForIdentity', (_req, res) => res.json([]));

// POST /getAllAssetTransfers — stub
app.post('/getAllAssetTransfers', (_req, res) => res.json([]));

// POST /findLog — stub (no log indexing on testnet)
app.post('/findLog', (_req, res) => res.json([]));

// POST /getlogcustom — stub
app.post('/getlogcustom', (_req, res) => res.json([]));

// POST /querySmartContract — proxy to node (Bob async interface not needed for testnet)
app.post('/querySmartContract', async (req, res) => {
    const { nonce, scIndex, funcNumber, data } = req.body || {};
    if (scIndex === undefined || funcNumber === undefined) {
        return res.status(400).json({ ok: false, error: 'Missing scIndex or funcNumber' });
    }
    try {
        const nodeRes = await rpcPost('/live/v1/querySmartContract', {
            contractIndex: scIndex,
            inputType:     funcNumber,
            inputHex:      data || '',
        });
        res.json({ nonce: nonce ?? 0, data: nodeRes.responseData ?? '' });
    } catch (err) {
        res.status(500).json({ ok: false, error: err.message });
    }
});

// GET /ticks/latest?limit=N — convenience endpoint (not in Bob, but useful for debugging)
app.get('/ticks/latest', (req, res) => {
    const limit = Math.min(parseInt(req.query.limit || '20'), 100);
    const rows  = db.prepare(
        'SELECT tick, epoch, timestamp, tx_count AS txCount FROM ticks ORDER BY tick DESC LIMIT ?'
    ).all(limit);
    res.json(rows);
});

app.listen(HTTP_PORT, () => {
    console.log(`[indexer] Bob-compatible API on http://0.0.0.0:${HTTP_PORT}`);
    console.log(`[indexer] Node: ${NODE_URL} | DB: ${DB_PATH}`);
});

// ── Bootstrap ─────────────────────────────────────────────────────────────────
async function bootstrap() {
    if (getLastIndexedTick() === 0) {
        try {
            const intervals = await rpcGet('/getProcessedTickIntervals');
            const firstTick = intervals?.[0]?.firstTick;
            if (firstTick && firstTick > 1) {
                setMeta.run('last_tick',    String(firstTick - 1));
                setMeta.run('initial_tick', String(firstTick));
                console.log(`[indexer] Fast-forwarded to epoch start tick ${firstTick}`);
            }
        } catch { /* node not ready yet */ }
    } else if (!getMeta.get('initial_tick')) {
        const row = db.prepare('SELECT MIN(tick) AS t FROM ticks').get();
        if (row?.t) setMeta.run('initial_tick', String(row.t));
    }
    setInterval(poll, POLL_MS);
    poll();
}

bootstrap();
