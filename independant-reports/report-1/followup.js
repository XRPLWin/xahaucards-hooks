#!/usr/bin/env node
/**
 * Second-pass live tests after restoring 8-byte PRICE/BUDGET/NEWACCT.
 * The first run overwrote those with >8-byte blobs, which made state_foreign(0,0)
 * return <=0 and poisoned later cases.
 */
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { derive, utils, signAndSubmit } from 'xrpl-accountlib'
import { XrplClient } from 'xrpl-client'
import { decodeAccountID } from 'ripple-address-codec'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const WSS = 'wss://xahau-test.net'
const EXPLORER_TX = 'https://test.xahauexplorer.com/explorer/'
const WALLET_FILE = path.join(__dirname, 'wallets.json')
const OUT = path.join(__dirname, 'followup-results.json')

const PRICE = 100_000
const BUDGET = 40_000
const NEWACCT = 200_000
const TF_PARTIAL = 0x00020000
const ASF_DEPOSIT_AUTH = 9
const ASF_DISALLOW_REMIT = 16

const wallets = JSON.parse(fs.readFileSync(WALLET_FILE, 'utf8'))
const tests = []
const findings = []
let client

function hex(buf) { return Buffer.from(buf).toString('hex').toUpperCase() }
function asciiHex(s) { return hex(Buffer.from(s, 'utf8')) }
function u32be(n) { const b = Buffer.alloc(4); b.writeUInt32BE(n >>> 0); return b }
function u64be(n) { const b = Buffer.alloc(8); b.writeBigUInt64BE(BigInt(n)); return b }
function accid(a) { return Buffer.from(decodeAccountID(a)) }
function hp(name, value) {
  return { HookParameter: { HookParameterName: asciiHex(name), HookParameterValue: hex(value) } }
}
function sleep(ms) { return new Promise((r) => setTimeout(r, ms)) }
function acct(role) { return derive.familySeed(wallets[role].seed) }
function rs(h) {
  if (!h) return ''
  try { return Buffer.from(h, 'hex').toString('utf8').replace(/\0+$/g, '') } catch { return String(h) }
}
function note(severity, title, detail) {
  findings.push({ severity, title, detail })
  console.log(`\n[FINDING ${severity}] ${title}\n  ${detail}\n`)
}

async function rpc(req) { return client.send(req) }

async function fetchTx(hash) {
  try { return await rpc({ command: 'tx', transaction: hash }) }
  catch (e) { return { error: e.message || String(e) } }
}

function hooksOf(tx) {
  return (tx?.meta?.HookExecutions || []).map((w) => {
    const h = w.HookExecution || w
    return {
      account: h.HookAccount,
      emit: h.HookEmitCount,
      result: h.HookResult,
      returnCode: h.HookReturnCode,
      msg: rs(h.HookReturnString),
    }
  })
}

async function submit(role, fields, label, opts = {}) {
  const account = acct(role)
  const net = await utils.txNetworkAndAccountValues(WSS, account)
  const tx = { ...fields, ...net.txValues, Account: wallets[role].address }
  try {
    const fee = await utils.networkTxFee(WSS, tx)
    if (fee) tx.Fee = String(Math.max(Number(fee), Number(opts.minFee || 0)))
  } catch { tx.Fee = String(opts.minFee || 100000) }
  let submitted
  try { submitted = await signAndSubmit(tx, WSS, account) }
  catch (e) {
    const rec = { id: label, engine: 'SUBMIT_THROW', error: e.message }
    tests.push(rec); console.log(`  ${label}: SUBMIT_THROW ${e.message}`); return rec
  }
  const hash = submitted?.tx_id || submitted?.txid || submitted?.hash
  await sleep(800)
  const full = hash ? await fetchTx(hash) : submitted
  const engine = full?.meta?.TransactionResult
    || submitted?.response?.engine_result
    || 'unknown'
  const hooks = hooksOf(full)
  const rec = { id: label, role, engine, hash, explorer: hash ? EXPLORER_TX + hash : undefined, hooks }
  tests.push(rec)
  console.log(`  ${label}: ${engine} — ${hooks.map((h) => h.msg).filter(Boolean).join(' | ') || ''} ${hash || ''}`)
  return rec
}

async function invoke(role, dest, params, label) {
  return submit(role, { TransactionType: 'Invoke', Destination: dest, HookParameters: params }, label)
}
async function pay(role, dest, drops, label, extra = {}) {
  return submit(role, { TransactionType: 'Payment', Destination: dest, Amount: String(drops), ...extra }, label)
}
async function adminWrite(pairs, label) {
  const params = [hp('NS', Buffer.from('S'))]
  pairs.forEach((p, i) => { params.push(hp('K' + i, p.key)); params.push(hp('V' + i, p.value)) })
  return invoke('issuer', wallets.issuer.address, params, label)
}

async function waitLedgers(n) {
  const info = await rpc({ command: 'ledger', ledger_index: 'validated' })
  const start = info?.ledger?.ledger_index || info?.ledger_index
  for (let i = 0; i < 40; i++) {
    const cur = await rpc({ command: 'ledger', ledger_index: 'validated' })
    const idx = cur?.ledger?.ledger_index || cur?.ledger_index
    if (idx >= start + n) return
    await sleep(1000)
  }
}

async function objects(account) {
  try { return (await rpc({ command: 'account_objects', account, ledger_index: 'validated', limit: 400 })).account_objects || [] }
  catch { return [] }
}
function uriTokens(objs) {
  return objs.filter((o) => o.LedgerEntryType === 'URIToken').map((o) => ({
    index: o.index,
    uri: o.URI ? Buffer.from(o.URI, 'hex').toString('utf8') : '',
    issuer: o.Issuer,
    owner: o.Owner,
  }))
}
function said(rec, s) { return (rec.hooks || []).some((h) => (h.msg || '').includes(s)) }

async function main() {
  console.log('Follow-up on existing testnet accounts:')
  for (const role of Object.keys(wallets)) {
    if (wallets[role]?.address) console.log(`  ${role.padEnd(10)} ${wallets[role].address}`)
  }

  client = new XrplClient(WSS)
  await client.ready()

  console.log('\n--- Restore 8-byte PRICE/BUDGET/NEWACCT ---')
  await adminWrite([
    { key: Buffer.from('PRICE'), value: u64be(PRICE) },
    { key: Buffer.from('BUDGET'), value: u64be(BUDGET) },
    { key: Buffer.from('NEWACCT'), value: u64be(NEWACCT) },
  ], 'restore 8-byte economics')

  const shop = wallets.shop.address
  const issuer = wallets.issuer.address
  const theme01 = [hp('THEME', Buffer.from('01', 'utf8'))]

  console.log('\n--- Shop checks with working price ---')
  const exact = await pay('buyer', shop, PRICE, 'buy exact 0.1 XAH', { HookParameters: theme01 })
  if (!(exact.engine === 'tesSUCCESS' && said(exact, 'paid'))) {
    note('HIGH', 'Exact pack price buy failed after restoring 8-byte PRICE', JSON.stringify(exact))
  }
  await waitLedgers(3)

  const wrong = await pay('buyer', shop, PRICE + 1, 'wrong amount', { HookParameters: theme01 })
  if (wrong.engine === 'tesSUCCESS') note('HIGH', 'Wrong pack amount accepted', wrong.hash)

  const noTheme = await pay('buyer', shop, PRICE, 'exact price no THEME')
  if (noTheme.engine === 'tesSUCCESS' || !said(noTheme, 'THEME')) {
    if (noTheme.engine === 'tesSUCCESS') note('MEDIUM', 'Missing THEME accepted', noTheme.hash)
    else console.log('    (no-theme reject reason):', (noTheme.hooks[0] || {}).msg)
  } else console.log('    OK missing THEME rejected')

  const t99 = await pay('buyer', shop, PRICE, 'THEME 99', { HookParameters: [hp('THEME', Buffer.from('99', 'utf8'))] })
  console.log('    theme99:', (t99.hooks[0] || {}).msg)

  const t03 = await pay('buyer', shop, PRICE, 'closed THEME 03', { HookParameters: [hp('THEME', Buffer.from('03', 'utf8'))] })
  console.log('    closed:', (t03.hooks[0] || {}).msg)

  const partial = await pay('buyer', shop, PRICE, 'partial flag', {
    Flags: TF_PARTIAL, SendMax: String(PRICE * 2), HookParameters: theme01,
  })
  console.log('    partial:', partial.engine, (partial.hooks[0] || {}).msg)

  console.log('\n--- Flag checks on outsider ---')
  await submit('outsider', { TransactionType: 'AccountSet', SetFlag: ASF_DISALLOW_REMIT }, 'outsider DisallowIncomingRemit')
  const remitBuy = await pay('outsider', shop, PRICE, 'buy with DisallowIncomingRemit', { HookParameters: theme01 })
  console.log('    remit-flag buy:', remitBuy.engine, (remitBuy.hooks[0] || {}).msg)
  if (remitBuy.engine === 'tesSUCCESS' && said(remitBuy, 'paid')) {
    note('HIGH', 'DisallowIncomingRemit did not block pack purchase', remitBuy.hash)
  }

  await submit('attestor', { TransactionType: 'AccountSet', SetFlag: ASF_DEPOSIT_AUTH }, 'attestor DepositAuth')
  const depBuy = await pay('attestor', shop, PRICE, 'buy with DepositAuth', { HookParameters: theme01 })
  console.log('    deposit-auth buy:', depBuy.engine, (depBuy.hooks[0] || {}).msg)
  if (depBuy.engine === 'tesSUCCESS' && said(depBuy, 'paid')) {
    note('HIGH', 'DepositAuth did not block pack purchase', depBuy.hash)
  }

  console.log('\n--- Grant allowance cap ---')
  await invoke('shop', shop, [hp('ALLOW', u32be(2))], 'ALLOW 2')
  const grantPayload = Buffer.concat([accid(wallets.buyer.address), Buffer.from('01', 'utf8')])
  const grants = []
  for (let i = 1; i <= 5; i++) {
    grants.push(await invoke('claim', shop, [hp('GRANT', grantPayload)], `GRANT ${i}/5`))
    await waitLedgers(1)
  }
  const gOk = grants.filter((g) => g.engine === 'tesSUCCESS').length
  console.log(`    successful GRANTs after ALLOW 2: ${gOk}`)
  if (gOk > 2) {
    note('CRITICAL', 'Grant allowance does not cap claims', `ALLOW 2 but ${gOk} GRANTs succeeded: ${grants.map((g) => g.engine + ':' + ((g.hooks[0] || {}).msg || '')).join(' || ')}`)
  } else if (gOk === 2) {
    console.log('    OK allowance held at 2')
  } else {
    note('MEDIUM', 'GRANTs failed before hitting the cap', grants.map((g) => ((g.hooks[0] || {}).msg || g.engine)).join(' || '))
  }

  const ghost = await invoke(
    'claim', shop,
    [hp('GRANT', Buffer.concat([accid(wallets.ghost.address), Buffer.from('01', 'utf8')]))],
    'GRANT to unfunded ghost',
  )
  console.log('    ghost:', ghost.engine, (ghost.hooks[0] || {}).msg)
  await waitLedgers(4)
  const ghostObjs = uriTokens(await objects(wallets.ghost.address))
  console.log('    ghost URITokens', ghostObjs.length, ghostObjs.map((t) => t.uri))

  console.log('\n--- Hostile table overflow (theme 02) ---')
  const ov = await pay('buyer', shop, PRICE, 'buy overflow theme 02', {
    HookParameters: [hp('THEME', Buffer.from('02', 'utf8'))],
  })
  await waitLedgers(4)
  if (ov.engine === 'tesSUCCESS' && said(ov, 'paid')) {
    const trig = ov.hash
    const orig = await fetchTx(trig)
    const emitted = orig?.meta?.HookEmissions?.[0]?.HookEmission?.EmittedTxnID
    let mintMsg = 'no trigger seen'
    if (emitted) {
      const m = await fetchTx(emitted)
      mintMsg = `${m?.meta?.TransactionResult} ${(hooksOf(m)[0] || {}).msg || ''}`
    }
    note('HIGH', 'Shop accepted payment for a hostile card-table row',
      `Theme 02 name_len=200. Buy ${ov.hash} trigger mint: ${mintMsg}. If mint failed, the 0.1 XAH stays in the shop with no refund.`)
  } else {
    console.log('    overflow buy blocked at shop:', ov.engine, (ov.hooks[0] || {}).msg)
  }

  console.log('\n--- More packs for edition increment ---')
  for (let i = 0; i < 3; i++) {
    await pay('buyer', shop, PRICE, `extra pack ${i + 1}/3`, { HookParameters: theme01 })
    await waitLedgers(3)
  }
  const buyerToks = uriTokens(await objects(wallets.buyer.address))
  console.log(`    buyer now has ${buyerToks.length} URITokens`)
  const editions = buyerToks.map((t) => t.uri)
  console.log(editions.map((u) => '      ' + u).join('\n'))
  const codes = {}
  for (const u of editions) {
    const base = u.replace(/-\d+$/, '')
    codes[base] = (codes[base] || 0) + 1
  }
  const dups = Object.entries(codes).filter(([, n]) => n > 1)
  if (dups.length) console.log('    repeated card codes (editions should climb):', dups)

  console.log('\n--- Attest ---')
  const card = buyerToks[0]
  if (card?.index) {
    const a1 = await invoke('attestor', issuer, [hp('ATTEST', Buffer.from(card.index, 'hex'))], 'ATTEST first card')
    await waitLedgers(2)
    const a2 = await invoke('attestor', issuer, [hp('ATTEST', Buffer.from(card.index, 'hex'))], 'ATTEST same card again')
    if (a2.engine === 'tesSUCCESS') note('MEDIUM', 'Duplicate CardSignature allowed', a2.hash)
    else console.log('    duplicate attest:', a2.engine, (a2.hooks[0] || {}).msg)

    const fake = await submit('issuer', { TransactionType: 'URITokenMint', URI: asciiHex('not-a-xahaucard') }, 'mint non-card URIToken')
    await waitLedgers(2)
    const issuerToks = uriTokens(await objects(issuer))
    const f = issuerToks.find((t) => t.uri.includes('not-a-xahaucard'))
    if (f?.index) {
      const af = await invoke('attestor', issuer, [hp('ATTEST', Buffer.from(f.index, 'hex'))], 'ATTEST non-card URIToken')
      if (af.engine === 'tesSUCCESS') {
        note('MEDIUM', 'Attestor signed a non-xahaucards URIToken issued by the issuer', `${f.uri} ${af.hash}`)
      } else console.log('    fake attest:', af.engine, (af.hooks[0] || {}).msg)
    } else {
      console.log('    fake token not found, mint engine', fake.engine)
    }
  }

  console.log('\n--- Mint amount / intent status ---')
  const shopPay = await pay('shop', issuer, 1, '1-drop shop->issuer (not emitted)')
  console.log('    unsigned 1-drop:', shopPay.engine, (shopPay.hooks[0] || {}).msg)

  fs.writeFileSync(OUT, JSON.stringify({ addresses: Object.fromEntries(Object.entries(wallets).filter(([, v]) => v.address).map(([k, v]) => [k, v.address])), findings, tests }, null, 2))
  console.log('\n========== FOLLOW-UP FINDINGS ==========')
  for (const f of findings) console.log(`- [${f.severity}] ${f.title}`)
  if (!findings.length) console.log('(no new confirmed findings; see test log)')
  console.log('Wrote', OUT)
  client.close?.()
  process.exit(0)
}

main().catch((e) => { console.error(e); process.exit(1) })
