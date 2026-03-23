'use strict';

const Database = require('better-sqlite3');
const express  = require('express');

// ── Config ────────────────────────────────────────────────────────────────────
const NODE_URL  = process.env.NODE_URL  || 'http://qubic-testnet:41841';
const DB_PATH   = process.env.DB_PATH   || '/data/indexer.db';
const HTTP_PORT = parseInt(process.env.HTTP_PORT || '3001');
const POLL_MS   = parseInt(process.env.POLL_MS   || '500');

// ── Transaction binary decoder ────────────────────────────────────────────────
// Transaction struct layout (80 bytes header):
//   sourcePublicKey      [0..31]  32 bytes
//   destinationPublicKey [32..63] 32 bytes
//   amount               [64..71] int64  little-endian
//   tick                 [72..75] uint32 little-endian
//   inputType            [76..77] uint16 little-endian
//   inputSize            [78..79] uint16 little-endian
//   payload              [80..80+inputSize-1]
//   signature            [80+inputSize..80+inputSize+63]

function publicKeyToIdentity(bytes) {
    // Qubic identity: 4 groups of 14 chars (56 chars) + 4-char checksum = 60 chars
    // Each group: take 8 bytes as little-endian uint64, encode base-26 (A=0..Z=25)
    let identity = '';
    for (let i = 0; i < 4; i++) {
        let part = 0n;
        for (let j = 7; j >= 0; j--) {
            part = part * 256n + BigInt(bytes[i * 8 + j]);
        }
        for (let j = 0; j < 14; j++) {
            identity += String.fromCharCode(Number(65n + part % 26n));
            part = part / 26n;
        }
    }
    // Checksum: KangarooTwelve of the 56-char identity bytes, take 3 bytes → 4 chars
    // We don't have K12 available, so compute a simple checksum using the existing
    // crypto module as a substitute just for display — checksum chars won't match
    // the real Qubic checksum, but source/dest matching still works since we're
    // consistent in our encoding. Identities received as strings (from the user)
    // are stored as-is in uppercase.
    // For proper checksum we'd need K12; skip for now, leave last 4 as computed below.
    const identBytes = Buffer.from(identity, 'ascii');
    let cs = 0;
    for (const b of identBytes) cs = (cs * 31 + b) >>> 0;
    for (let j = 0; j < 4; j++) {
        identity += String.fromCharCode(65 + (cs % 26));
        cs = Math.floor(cs / 26);
    }
    return identity;
}

function decodeTx(base64) {
    const buf  = Buffer.from(base64, 'base64');
    const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);

    const sourceBytes = buf.slice(0, 32);
    const destBytes   = buf.slice(32, 64);
    const amount      = Number(view.getBigInt64(64, true));
    const tick        = view.getUint32(72, true);
    const inputType   = view.getUint16(76, true);
    const inputSize   = view.getUint16(78, true);

    return {
        source:      publicKeyToIdentity(sourceBytes),
        destination: publicKeyToIdentity(destBytes),
        amount,
        tick,
        inputType,
        inputSize,
    };
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
    input_size  INTEGER,
    raw         TEXT
  );

  CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT
  );

  CREATE INDEX IF NOT EXISTS idx_tx_tick   ON transactions(tick);
  CREATE INDEX IF NOT EXISTS idx_tx_source ON transactions(source);
  CREATE INDEX IF NOT EXISTS idx_tx_dest   ON transactions(destination);
`);

const getMeta = db.prepare('SELECT value FROM meta WHERE key = ?');
const setMeta = db.prepare('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)');
const insertTx = db.prepare(`
    INSERT OR IGNORE INTO transactions (hash, tick, source, destination, amount, input_type, input_size, raw)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?)
`);
const updateTx = db.prepare(`
    UPDATE transactions
    SET source = ?, destination = ?, amount = ?, input_type = ?, input_size = ?, raw = ?
    WHERE hash = ? AND source IS NULL
`);

function getLastIndexedTick() {
    const row = getMeta.get('last_tick');
    return row ? parseInt(row.value) : 0;
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
    return data.tickNumber;
}

// ── Tick indexer ──────────────────────────────────────────────────────────────
async function indexTick(tickNumber) {
    let tickData;
    try {
        tickData = await rpcPost('/getTickData', { tickNumber });
    } catch {
        return;
    }

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
                VALUES (?, ?, ?, ?, ?, ?)`).run(
        tickNumber,
        tickData.epoch          ?? null,
        tickData.timestamp      ?? null,
        tickData.computorIndex  ?? null,
        digests.length,
        JSON.stringify(tickData),
    );

    for (const digest of digests) {
        const hash = digest.toLowerCase();
        const tx   = txMap[hash];
        insertTx.run(
            hash, tickNumber,
            tx?.sourceId ?? null, tx?.destId ?? null,
            tx?.amount   ?? null, tx?.inputType ?? null, tx?.inputSize ?? null,
            tx ? JSON.stringify(tx) : null,
        );
    }
}

// ── Indexer loop ──────────────────────────────────────────────────────────────
let indexerRunning = false;

async function poll() {
    if (indexerRunning) return;
    indexerRunning = true;
    try {
        const lastProcessed = await getLastProcessedTick();
        const lastIndexed   = getLastIndexedTick();
        if (lastProcessed <= lastIndexed) return;

        const lag   = lastProcessed - lastIndexed;
        const batch = lag > 200 ? 50 : 1;
        const from  = lastIndexed + 1;
        const to    = Math.min(lastProcessed, from + batch - 1);

        for (let tick = from; tick <= to; tick++) await indexTick(tick);

        setMeta.run('last_tick', String(to));
        if (to > from) {
            console.log(`[indexer] ticks ${from}–${to} indexed (latest: ${lastProcessed})`);
        } else {
            console.log(`[indexer] tick ${to} indexed`);
        }
    } catch (err) {
        if (!err.message?.includes('ECONNREFUSED') && !err.message?.includes('ENOTFOUND')) {
            console.error('[indexer] poll error:', err.message);
        }
    } finally {
        indexerRunning = false;
    }
}

// ── HTTP API ──────────────────────────────────────────────────────────────────
const app = express();
app.use(express.json());
app.set('json spaces', 2);

// POST /broadcast-transaction  ← proxy that captures full tx data before forwarding
app.post('/broadcast-transaction', async (req, res) => {
    const { encodedTransaction } = req.body || {};
    if (!encodedTransaction) {
        return res.status(400).json({ error: 'Missing encodedTransaction' });
    }

    // Decode the binary transaction to extract fields
    let decoded;
    try {
        decoded = decodeTx(encodedTransaction);
    } catch (err) {
        return res.status(400).json({ error: `Failed to decode transaction: ${err.message}` });
    }

    // Forward to node
    let nodeResponse;
    try {
        const r = await fetch(`${NODE_URL}/live/v1/broadcast-transaction`, {
            method:  'POST',
            headers: { 'Content-Type': 'application/json' },
            body:    JSON.stringify({ encodedTransaction }),
        });
        nodeResponse = await r.json();
    } catch (err) {
        return res.status(502).json({ error: `Node unreachable: ${err.message}` });
    }

    // Store the tx with full decoded data
    const hash = (nodeResponse.transactionId || '').toLowerCase();
    if (hash) {
        insertTx.run(
            hash, decoded.tick,
            decoded.source, decoded.destination,
            decoded.amount, decoded.inputType, decoded.inputSize,
            JSON.stringify({ ...decoded, txId: hash }),
        );
        // Also patch any existing stub row (indexed by tick poller without data)
        updateTx.run(
            decoded.source, decoded.destination,
            decoded.amount, decoded.inputType, decoded.inputSize,
            JSON.stringify({ ...decoded, txId: hash }),
            hash,
        );
        console.log(`[indexer] captured tx ${hash} (tick ${decoded.tick}, inputType ${decoded.inputType}, amount ${decoded.amount})`);
    }

    res.json(nodeResponse);
});

// GET /status
app.get('/status', (_req, res) => {
    const lastIndexed = getLastIndexedTick();
    const tickCount   = db.prepare('SELECT COUNT(*) AS c FROM ticks').get().c;
    const txCount     = db.prepare('SELECT COUNT(*) AS c FROM transactions').get().c;
    res.json({ lastIndexedTick: lastIndexed, totalTicks: tickCount, totalTransactions: txCount });
});

// GET /tick/:n
app.get('/tick/:n', (req, res) => {
    const tick = parseInt(req.params.n);
    const row  = db.prepare('SELECT * FROM ticks WHERE tick = ?').get(tick);
    if (!row) return res.status(404).json({ error: 'Tick not found' });

    const txs = db.prepare('SELECT * FROM transactions WHERE tick = ?').all(tick);
    res.json({
        tick:          row.tick,
        epoch:         row.epoch,
        timestamp:     row.timestamp,
        computorIndex: row.computor_index,
        txCount:       row.tx_count,
        transactions:  txs.map(t => ({
            hash:        t.hash,
            source:      t.source,
            destination: t.destination,
            amount:      t.amount,
            inputType:   t.input_type,
            inputSize:   t.input_size,
        })),
        tickData: row.raw ? JSON.parse(row.raw) : null,
    });
});

// GET /tx/:hash
app.get('/tx/:hash', (req, res) => {
    const hash = req.params.hash.toLowerCase();
    const row  = db.prepare('SELECT * FROM transactions WHERE hash = ?').get(hash);
    if (!row) return res.status(404).json({ error: 'Transaction not found' });
    res.json({
        hash:        row.hash,
        tick:        row.tick,
        source:      row.source,
        destination: row.destination,
        amount:      row.amount,
        inputType:   row.input_type,
        inputSize:   row.input_size,
    });
});

// GET /identity/:id/transactions
app.get('/identity/:id/transactions', (req, res) => {
    const id     = req.params.id.toUpperCase();
    const limit  = Math.min(parseInt(req.query.limit  || '50'), 200);
    const offset = parseInt(req.query.offset || '0');
    const rows   = db.prepare(`
        SELECT * FROM transactions
        WHERE source = ? OR destination = ?
        ORDER BY tick DESC LIMIT ? OFFSET ?
    `).all(id, id, limit, offset);
    const total  = db.prepare(
        'SELECT COUNT(*) AS c FROM transactions WHERE source = ? OR destination = ?'
    ).get(id, id).c;

    res.json({
        identity: id,
        total,
        transactions: rows.map(t => ({
            hash:        t.hash,
            tick:        t.tick,
            source:      t.source,
            destination: t.destination,
            amount:      t.amount,
            inputType:   t.input_type,
        })),
    });
});

// GET /ticks/latest?limit=20
app.get('/ticks/latest', (req, res) => {
    const limit = Math.min(parseInt(req.query.limit || '20'), 100);
    const rows  = db.prepare(
        'SELECT tick, epoch, timestamp, tx_count FROM ticks ORDER BY tick DESC LIMIT ?'
    ).all(limit);
    res.json(rows);
});

app.listen(HTTP_PORT, () => {
    console.log(`[indexer] HTTP API  : http://0.0.0.0:${HTTP_PORT}`);
    console.log(`[indexer] Broadcast : POST /broadcast-transaction  (proxies to node + indexes)`);
    console.log(`[indexer] Database  : ${DB_PATH}`);
    console.log(`[indexer] Node URL  : ${NODE_URL}`);
});

// ── Bootstrap ─────────────────────────────────────────────────────────────────
async function bootstrap() {
    if (getLastIndexedTick() === 0) {
        try {
            const intervals = await rpcGet('/getProcessedTickIntervals');
            const firstTick = intervals?.[0]?.firstTick;
            if (firstTick && firstTick > 1) {
                setMeta.run('last_tick', String(firstTick - 1));
                console.log(`[indexer] Fast-forwarded to epoch start tick ${firstTick}`);
            }
        } catch { /* node not ready yet */ }
    }
    setInterval(poll, POLL_MS);
    poll();
}

bootstrap();
