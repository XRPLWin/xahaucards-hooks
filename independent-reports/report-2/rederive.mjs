// Independent re-derivation of a real XahauCards pack from PUBLIC DATA ONLY.
// seed = sha512half(parent_payment_txid || ledger_last_hash); card[i]=sha512half(seed||i); 7 bytes used.
// Proves the roll is a pure function of chain data (no operator lever) by matching re-derived codes to on-chain URIs.
import p from 'xahau';
import { createHash } from 'node:crypto';
const ISS = 'rhi2ndQkEmV5x1t7YisEKUF3J8tw4VnJ83';
const NS_TABLE = '2802C8663C08E0E970302AE14225A26358A6D8D07C333540E3A73F8401BEF8C1';
const sha512h = (buf) => createHash('sha512').update(buf).digest().subarray(0, 32);
const RARITY = ['C', 'U', 'R', 'E', 'L'];
const BAND_LO = [1, 5, 9, 13, 20], BAND_HI = [4, 8, 12, 19, 20];
const pad3 = (n) => String(n).padStart(3, '0'); const pad2 = (n) => String(n).padStart(2, '0');

function deriveCard(seed, idx, shape, theme) {
  const roll = sha512h(Buffer.concat([seed, Buffer.from([idx])]));
  const rarity = (roll[0] >= 150) + (roll[0] >= 220) + (roll[0] >= 246) + (roll[0] >= 254);
  const start = (shape[rarity * 4] << 8) | shape[rarity * 4 + 1];
  const filled = (shape[rarity * 4 + 2] << 8) | shape[rarity * 4 + 3];
  const subject = start + (((roll[1] << 8) | roll[2]) % filled);
  const spread = BAND_HI[rarity] - BAND_LO[rarity] + 1;
  const attack = BAND_LO[rarity] + (((roll[3] << 8) | roll[4]) % spread);
  const health = BAND_LO[rarity] + (((roll[5] << 8) | roll[6]) % spread);
  return `XC-${theme}-${pad3(subject)}-${RARITY[rarity]}-${pad2(attack)}-${pad2(health)}`;
}

const c = new p.Client('wss://xahau-test.net', { connectionTimeout: 15000 }); await c.connect();

// shape (D-record) for theme 01
const dkey = ('00'.repeat(29) + Buffer.from('D01', 'utf8').toString('hex')).toUpperCase();
const st = await c.request({ command: 'ledger_entry', hook_state: { account: ISS, key: dkey, namespace_id: NS_TABLE }, ledger_index: 'validated' });
const shape = Buffer.from(st.result.node.HookStateData, 'hex');

// find a trigger Payment (to issuer, carrying EmitParentTxnID = buyer payment P)
const tx = await c.request({ command: 'account_tx', account: ISS, ledger_index_min: -1, ledger_index_max: -1, limit: 80, forward: false });
let trig = null;
for (const t of tx.result.transactions) {
  const o = t.tx || t.tx_json || {};
  if (o.TransactionType === 'Payment' && o.Destination === ISS && o.EmitDetails?.EmitParentTxnID) {
    trig = { T: o.hash || t.hash, P: o.EmitDetails.EmitParentTxnID, M: t.tx?.ledger_index ?? t.ledger_index ?? o.ledger_index };
    break;
  }
}
if (!trig) { console.log('no trigger found in window'); await c.disconnect(); process.exit(1); }
console.log('purchase chain: buyer payment P =', trig.P);
console.log('               trigger T =', trig.T, ' (issuer ledger M =', trig.M, ')');

// ledger_last_hash seen by the mint = parentHash of M = hash(M-1) = the ledger P is in
const lh = await c.request({ command: 'ledger', ledger_index: trig.M - 1 });
const lastHash = (lh.result.ledger_hash || lh.result.ledger.ledger_hash);
console.log('ledger_last_hash (hash of M-1) =', lastHash);

// the 5 minted cards: Remits with EmitParentTxnID = T
const onchain = [];
for (const t of tx.result.transactions) {
  const o = t.tx || t.tx_json || {}; const meta = t.meta || {};
  if (o.TransactionType === 'Remit' && o.EmitDetails?.EmitParentTxnID === trig.T) {
    for (const n of (meta.AffectedNodes || [])) { const cn = n.CreatedNode; if (cn?.LedgerEntryType === 'URIToken') { const uri = Buffer.from(cn.NewFields.URI, 'hex').toString(); onchain.push(uri.replace('xahaucards://', '').replace(/-\d+$/, '')); } }
  }
}
console.log('\non-chain minted codes (' + onchain.length + '):', onchain);

// RE-DERIVE from public data
const seed = sha512h(Buffer.concat([Buffer.from(trig.P, 'hex'), Buffer.from(lastHash, 'hex')]));
const derived = [0, 1, 2, 3, 4].map((i) => deriveCard(seed, i, shape, '01'));
console.log('re-derived codes (public data):     ', derived);

// compare as multisets (mint order vs on-chain order may differ)
const norm = (a) => [...a].sort().join('|');
const match = onchain.length === 5 && norm(onchain) === norm(derived);
console.log('\n==== MATCH:', match, '====');
if (!match) console.log('  on-chain sorted:', norm(onchain), '\n  derived sorted: ', norm(derived));
await c.disconnect();
process.exit(match ? 0 : 2);
