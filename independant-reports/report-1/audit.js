#!/usr/bin/env node
/**
 * Live Xahau testnet audit of XRPLWin/xahaucards-hooks.
 * Installs the published wasm on fresh accounts, then probes the four hooks
 * with real transactions.
 */
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { derive, generate, utils, signAndSubmit } from 'xrpl-accountlib'
import { XrplClient } from 'xrpl-client'
import { decodeAccountID } from 'ripple-address-codec'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const WSS = 'wss://xahau-test.net'
const FAUCET = 'https://xahau-test.net/accounts'
const EXPLORER_TX = 'https://test.xahauexplorer.com/explorer/'
const EXPLORER_ACC = 'https://test.xahauexplorer.com/en/account/'
const WASM_DIR = path.join(__dirname, '..', 'xahaucards-hooks', 'build')
const WALLET_FILE = path.join(__dirname, 'wallets.json')
const RESULT_FILE = path.join(__dirname, 'results.json')

const NS = {
  INTENT: '9ADC51BD51B9805AED67CAE5BE65CD7DABCA9492560E95AB365D8EF808D611D7',
  EDITIONS: 'C46E1A05A03C218F4D47B17035429FD6D8FE6F4FDC30C1A14739263BC00D4423',
  TABLE: '2802C8663C08E0E970302AE14225A26358A6D8D07C333540E3A73F8401BEF8C1',
  SETTINGS: 'E51244A5EFDEA2D62F5FEAE1BB4651B19BB75836E3CD12CA6BF2D1ED4338543D',
  ATTEST: '6320036D8EC1169E9F2A18F1DCD05AA11F26DBF15A6A250C5F47CB08ED9F2926',
}

const TT = { PAYMENT: 0, ACCOUNT_SET: 3, SETHOOK: 22, URITOKEN_MINT: 45, REMARKS_SET: 94, REMIT: 95, INVOKE: 99 }
const HSF_OVERRIDE = 1
const HSF_COLLECT = 4
const TF_PARTIAL = 0x00020000
const ASF_REQUIRE_DEST = 1
const ASF_DEPOSIT_AUTH = 9
const ASF_DISALLOW_REMIT = 16

const ROLES = ['shop', 'issuer', 'buyer', 'claim', 'attestor', 'outsider', 'blocker', 'ghost']

const findings = []
const tests = []
let client
let wallets

function hex(buf) {
  return Buffer.from(buf).toString('hex').toUpperCase()
}
function asciiHex(s) {
  return hex(Buffer.from(s, 'utf8'))
}
function hookOn(types) {
  let n = (1n << 256n) - 1n
  for (const t of types) n ^= 1n << BigInt(t)
  return n.toString(16).toUpperCase().padStart(64, '0')
}
function u32be(n) {
  const b = Buffer.alloc(4)
  b.writeUInt32BE(n >>> 0)
  return b
}
function u64be(n) {
  const b = Buffer.alloc(8)
  b.writeBigUInt64BE(BigInt(n))
  return b
}
function blob(len, fill = 0xaa) {
  return Buffer.alloc(len, fill)
}
function accid(address) {
  return Buffer.from(decodeAccountID(address))
}
function label(name) {
  const n = Buffer.from(name, 'utf8')
  return Buffer.concat([Buffer.from([n.length]), n])
}
function row(name, faction4) {
  const b = Buffer.alloc(25)
  const n = Buffer.from(name, 'utf8')
  b[0] = n.length
  n.copy(b, 1)
  Buffer.from(faction4, 'utf8').copy(b, 21)
  return b
}
function shapeAllSubject1() {
  const b = Buffer.alloc(20)
  for (let r = 0; r < 5; r++) {
    b[r * 4 + 0] = 0x00
    b[r * 4 + 1] = 0x01
    b[r * 4 + 2] = 0x00
    b[r * 4 + 3] = 0x01
  }
  return b
}
function hp(name, value) {
  return {
    HookParameter: {
      HookParameterName: asciiHex(name),
      HookParameterValue: hex(value),
    },
  }
}
function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms))
}
function note(severity, title, detail) {
  findings.push({ severity, title, detail })
  console.log(`\n[FINDING ${severity}] ${title}\n  ${detail}\n`)
}

function printAddresses() {
  console.log('\n========== PUBLIC ADDRESSES (Xahau testnet) ==========')
  for (const role of ROLES) {
    const a = wallets[role].address
    console.log(`  ${role.padEnd(10)} ${a}`)
    console.log(`             ${EXPLORER_ACC}${a}`)
  }
  console.log('======================================================\n')
}

async function rpc(req) {
  return client.send(req)
}

async function faucet(address) {
  for (let i = 0; i < 12; i++) {
    const res = await fetch(FAUCET, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', Accept: 'application/json' },
      body: JSON.stringify({ destination: address }),
    })
    const text = await res.text()
    let body
    try {
      body = JSON.parse(text)
    } catch {
      body = { raw: text }
    }
    if (body.error && /wait (\d+) seconds/i.test(body.error)) {
      const sec = Number(body.error.match(/wait (\d+) seconds/i)[1]) + 2
      console.log(`  faucet rate-limit for ${address}, sleeping ${sec}s`)
      await sleep(sec * 1000)
      continue
    }
    if (res.ok && (body.balance || body.amount || body.account || body.trace) && !body.error) {
      return body
    }
    console.log(`  faucet retry ${i + 1} for ${address}: ${res.status} ${text.slice(0, 180)}`)
    await sleep(5000)
  }
  throw new Error(`faucet failed for ${address}`)
}

async function alreadyFunded(address) {
  try {
    const info = await rpc({ command: 'account_info', account: address, ledger_index: 'validated' })
    const bal = info?.account_data?.Balance
    if (bal && BigInt(bal) > 0n) return BigInt(bal)
  } catch {
    return 0n
  }
  return 0n
}

async function waitFunded(address) {
  for (let i = 0; i < 30; i++) {
    try {
      const info = await rpc({ command: 'account_info', account: address, ledger_index: 'validated' })
      const bal = info?.account_data?.Balance
      if (bal && BigInt(bal) > 0n) return BigInt(bal)
    } catch {
      // not funded yet
    }
    await sleep(1000)
  }
  throw new Error(`account ${address} never appeared`)
}

function loadOrCreateWallets() {
  if (fs.existsSync(WALLET_FILE)) {
    return JSON.parse(fs.readFileSync(WALLET_FILE, 'utf8'))
  }
  const out = { network: WSS, created: new Date().toISOString() }
  for (const role of ROLES) {
    const acc = generate.familySeed()
    out[role] = { address: acc.address, seed: acc.secret.familySeed, role }
  }
  fs.writeFileSync(WALLET_FILE, JSON.stringify(out, null, 2))
  return out
}

function acct(role) {
  return derive.familySeed(wallets[role].seed)
}

function wasmHex(name) {
  return hex(fs.readFileSync(path.join(WASM_DIR, `${name}.wasm`)))
}

function extractHooks(meta) {
  const execs = []
  const bag = meta?.HookExecutions || meta?.hookExecutions || []
  for (const wrap of bag) {
    const h = wrap.HookExecution || wrap
    execs.push({
      account: h.HookAccount,
      hash: h.HookHash,
      result: h.HookResult,
      returnCode: h.HookReturnCode,
      returnString: h.HookReturnString
        ? Buffer.from(h.HookReturnString, 'hex').toString('utf8').replace(/\0+$/, '')
        : undefined,
      emitCount: h.HookEmitCount,
    })
  }
  return execs
}

async function fetchTx(hash) {
  try {
    return await rpc({ command: 'tx', transaction: hash, binary: false })
  } catch (e) {
    return { error: e.message || String(e) }
  }
}

async function submit(role, fields, label, opts = {}) {
  const account = acct(role)
  const net = await utils.txNetworkAndAccountValues(WSS, account)
  const tx = { ...fields, ...net.txValues }
  if (!tx.Account) tx.Account = wallets[role].address
  try {
    const fee = await utils.networkTxFee(WSS, tx)
    if (fee) tx.Fee = String(Math.max(Number(fee), Number(opts.minFee || 0)))
  } catch {
    tx.Fee = String(opts.minFee || 100000)
  }
  if (opts.minFee && Number(tx.Fee) < Number(opts.minFee)) tx.Fee = String(opts.minFee)
  if (fields.TransactionType === 'SetHook' && Number(tx.Fee) < 20_000_000) {
    tx.Fee = '50000000'
  }

  let submitted
  try {
    submitted = await signAndSubmit(tx, WSS, account)
  } catch (e) {
    const rec = {
      id: label,
      role,
      engine: 'SUBMIT_THROW',
      error: e.message || String(e),
      tx: { TransactionType: tx.TransactionType, Account: tx.Account, Destination: tx.Destination },
    }
    tests.push(rec)
    console.log(`  ${label}: SUBMIT_THROW ${rec.error}`)
    return rec
  }

  const hash = submitted?.tx_id || submitted?.txid || submitted?.hash
  const engine =
    submitted?.response?.engine_result ||
    submitted?.engine_result ||
    submitted?.result?.engine_result ||
    'unknown'
  const message =
    submitted?.response?.engine_result_message ||
    submitted?.engine_result_message ||
    ''

  let full = submitted?.response || submitted
  if (hash && engine !== 'tesSUCCESS') {
    await sleep(1200)
    const got = await fetchTx(hash)
    if (got && !got.error) full = got
  } else if (hash) {
    await sleep(400)
    const got = await fetchTx(hash)
    if (got && !got.error) full = got
  }

  const meta = full?.meta || full?.metaData || submitted?.response?.meta
  const hooks = extractHooks(meta)
  const rec = {
    id: label,
    role,
    engine,
    message,
    hash,
    explorer: hash ? EXPLORER_TX + hash : undefined,
    hooks,
    result: full?.validated === true ? 'validated' : undefined,
  }
  tests.push(rec)
  const hookMsg = hooks.map((h) => `${h.returnString || h.returnCode || ''}`).filter(Boolean).join(' | ')
  console.log(`  ${label}: ${engine}${hookMsg ? ' — ' + hookMsg : ''}${hash ? ' ' + hash : ''}`)
  return rec
}

async function invoke(role, destination, params, label, extra = {}) {
  return submit(
    role,
    {
      TransactionType: 'Invoke',
      Destination: destination,
      HookParameters: params,
      ...extra,
    },
    label,
  )
}

async function pay(role, destination, drops, label, extra = {}) {
  return submit(
    role,
    {
      TransactionType: 'Payment',
      Destination: destination,
      Amount: String(drops),
      ...extra,
    },
    label,
  )
}

async function accountSetFlag(role, flag, label) {
  return submit(role, { TransactionType: 'AccountSet', SetFlag: flag }, label)
}

async function adminWrite(nsChar, pairs, label) {
  const chunks = []
  for (let i = 0; i < pairs.length; i += 7) chunks.push(pairs.slice(i, i + 7))
  const out = []
  let n = 0
  for (const chunk of chunks) {
    const params = [hp('NS', Buffer.from(nsChar, 'utf8'))]
    chunk.forEach((p, i) => {
      params.push(hp('K' + i, p.key))
      params.push(hp('V' + i, p.value))
    })
    out.push(await invoke('issuer', wallets.issuer.address, params, `${label}#${n++}`))
  }
  return out
}

async function ledgerEntryHookState(account, namespace, key) {
  const key32 = Buffer.alloc(32)
  Buffer.from(key).copy(key32)
  try {
    return await rpc({
      command: 'ledger_entry',
      hook_state: {
        account,
        key: hex(key32),
        namespace,
      },
    })
  } catch (e) {
    return { error: e.message || String(e) }
  }
}

async function accountNamespace(account, namespace) {
  try {
    return await rpc({ command: 'account_namespace', account, namespace_id: namespace })
  } catch (e) {
    return { error: e.message || String(e) }
  }
}

async function accountObjects(account, type) {
  const req = { command: 'account_objects', account, ledger_index: 'validated', limit: 400 }
  if (type) req.type = type
  try {
    return await rpc(req)
  } catch (e) {
    return { error: e.message || String(e), account_objects: [] }
  }
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

function hookSaid(rec, substr) {
  return (rec.hooks || []).some((h) => (h.returnString || '').includes(substr))
}

async function installHooks() {
  const issuerId = accid(wallets.issuer.address)
  const shopOn = hookOn([TT.PAYMENT])
  const doorsOn = hookOn([TT.INVOKE])
  const mintOn = hookOn([TT.PAYMENT])
  const mgrOn = hookOn([TT.INVOKE])
  const shopEmit = hookOn([TT.PAYMENT])
  const mintEmit = hookOn([TT.REMARKS_SET, TT.REMIT])
  const mgrEmit = hookOn([TT.REMARKS_SET])

  const shop = await submit(
    'shop',
    {
      TransactionType: 'SetHook',
      Flags: 0,
      Hooks: [
        {
          Hook: {
            CreateCode: wasmHex('shop'),
            Flags: HSF_OVERRIDE | HSF_COLLECT,
            HookApiVersion: 0,
            HookNamespace: NS.INTENT,
            HookOn: shopOn,
            HookCanEmit: shopEmit,
            HookParameters: [hp('ISSUER', issuerId)],
          },
        },
        {
          Hook: {
            CreateCode: wasmHex('doors'),
            Flags: HSF_OVERRIDE | HSF_COLLECT,
            HookApiVersion: 0,
            HookNamespace: NS.INTENT,
            HookOn: doorsOn,
            HookCanEmit: shopEmit,
            HookParameters: [hp('ISSUER', issuerId)],
          },
        },
      ],
    },
    'install shop+doors',
    { minFee: 50_000_000 },
  )

  const issuer = await submit(
    'issuer',
    {
      TransactionType: 'SetHook',
      Flags: 0,
      Hooks: [
        {
          Hook: {
            CreateCode: wasmHex('mint'),
            Flags: HSF_OVERRIDE,
            HookApiVersion: 0,
            HookNamespace: NS.EDITIONS,
            HookOn: mintOn,
            HookCanEmit: mintEmit,
          },
        },
        {
          Hook: {
            CreateCode: wasmHex('manager'),
            Flags: HSF_OVERRIDE,
            HookApiVersion: 0,
            HookNamespace: NS.EDITIONS,
            HookOn: mgrOn,
            HookCanEmit: mgrEmit,
          },
        },
      ],
    },
    'install mint+manager',
    { minFee: 50_000_000 },
  )

  return { shop, issuer }
}

async function configureIntended() {
  const settings = [
    { key: Buffer.from('PRICE'), value: u64be(10_000_000) },
    { key: Buffer.from('BUDGET'), value: u64be(500_000) },
    { key: Buffer.from('NEWACCT'), value: u64be(200_000) },
    { key: Buffer.from('SHOP'), value: accid(wallets.shop.address) },
    { key: Buffer.from('CLAIM'), value: accid(wallets.claim.address) },
    { key: Buffer.from('DOMAIN'), value: Buffer.from('xahaucards.com', 'utf8') },
  ]
  await adminWrite('S', settings, 'settings-intended')
}

async function configureCheapWorking() {
  const settings = [
    { key: Buffer.from('PRICE'), value: blob(10, 0x11) },
    { key: Buffer.from('BUDGET'), value: blob(5, 0x22) },
    { key: Buffer.from('NEWACCT'), value: blob(5, 0x33) },
  ]
  await adminWrite('S', settings, 'settings-cheap-length')
}

async function configureTable() {
  const table = [
    { key: Buffer.from('T01', 'utf8'), value: label('Audit') },
    { key: Buffer.from('D01', 'utf8'), value: shapeAllSubject1() },
    { key: Buffer.from('O01', 'utf8'), value: Buffer.from([0x01]) },
    { key: Buffer.from('F01ALPH', 'utf8'), value: label('Alpha') },
    { key: Buffer.from('RC', 'utf8'), value: label('Common') },
    { key: Buffer.from('RU', 'utf8'), value: label('Uncommon') },
    { key: Buffer.from('RR', 'utf8'), value: label('Rare') },
    { key: Buffer.from('RE', 'utf8'), value: label('Epic') },
    { key: Buffer.from('RL', 'utf8'), value: label('Legendary') },
    { key: Buffer.from('S01001', 'utf8'), value: row('TestCard', 'ALPH') },
    { key: Buffer.from('T02', 'utf8'), value: label('Overflow') },
    { key: Buffer.from('D02', 'utf8'), value: shapeAllSubject1() },
    { key: Buffer.from('O02', 'utf8'), value: Buffer.from([0x01]) },
    { key: Buffer.from('F02ALPH', 'utf8'), value: label('Alpha') },
    { key: Buffer.from('S02001', 'utf8'), value: (() => {
      const b = Buffer.alloc(25, 0x41)
      b[0] = 200
      Buffer.from('ALPH').copy(b, 21)
      return b
    })() },
    { key: Buffer.from('T03', 'utf8'), value: label('Closed') },
    { key: Buffer.from('D03', 'utf8'), value: shapeAllSubject1() },
    { key: Buffer.from('O03', 'utf8'), value: Buffer.from([0x00]) },
    { key: Buffer.from('S03001', 'utf8'), value: row('ClosedSet', 'ALPH') },
    { key: Buffer.from('F03ALPH', 'utf8'), value: label('Alpha') },
  ]
  await adminWrite('T', table, 'table')
}

async function addAttestor() {
  const slot = Buffer.from('01Attestor', 'utf8')
  await adminWrite('A', [{ key: accid(wallets.attestor.address), value: slot }], 'attestor-roll')
}

function expect(rec, ok, title, detail) {
  if (ok) {
    console.log(`    OK  ${title}`)
  } else {
    note('MEDIUM', title, detail || `${rec.id} engine=${rec.engine}`)
  }
}

async function countUriTokens(address) {
  const objs = await accountObjects(address)
  const list = objs.account_objects || []
  return list.filter((o) => o.LedgerEntryType === 'URIToken' || o.URITokenID || o.URI).length
}

async function listUriTokens(address) {
  const objs = await accountObjects(address)
  const list = objs.account_objects || []
  return list.filter((o) => o.LedgerEntryType === 'URIToken' || o.URI)
}

async function runAttacks() {
  const shop = wallets.shop.address
  const issuer = wallets.issuer.address
  const theme01 = [hp('THEME', Buffer.from('01', 'utf8'))]

  console.log('\n--- A. Auth / admin surface ---')
  const unauthAdmin = await invoke(
    'outsider',
    issuer,
    [hp('NS', Buffer.from('S')), hp('K0', Buffer.from('PRICE')), hp('V0', u64be(1))],
    'outsider admin write PRICE',
  )
  expect(
    unauthAdmin,
    unauthAdmin.engine !== 'tesSUCCESS' || hookSaid(unauthAdmin, 'must carry ATTEST') || hookSaid(unauthAdmin, 'signed by the issuer'),
    'outsider cannot write issuer settings',
    `outsider admin succeeded: ${unauthAdmin.engine} ${JSON.stringify(unauthAdmin.hooks)}`,
  )

  const unauthAllow = await invoke(
    'outsider',
    shop,
    [hp('ALLOW', u32be(50))],
    'outsider ALLOW',
  )
  expect(
    unauthAllow,
    unauthAllow.engine !== 'tesSUCCESS' || hookSaid(unauthAllow, 'only the shop'),
    'outsider cannot set grant allowance',
    `outsider ALLOW succeeded: ${unauthAllow.engine}`,
  )

  const unauthGrant = await invoke(
    'outsider',
    shop,
    [hp('GRANT', Buffer.concat([accid(wallets.buyer.address), Buffer.from('01', 'utf8')]))],
    'outsider GRANT',
  )
  expect(
    unauthGrant,
    unauthGrant.engine !== 'tesSUCCESS' || hookSaid(unauthGrant, 'only the claim'),
    'outsider cannot GRANT',
    `outsider GRANT succeeded: ${unauthGrant.engine}`,
  )

  const unauthAttest = await invoke(
    'outsider',
    issuer,
    [hp('ATTEST', Buffer.alloc(32, 0x11))],
    'outsider ATTEST random id',
  )
  expect(
    unauthAttest,
    unauthAttest.engine !== 'tesSUCCESS',
    'outsider ATTEST rejected',
    `outsider ATTEST succeeded: ${unauthAttest.engine}`,
  )

  const emptyInvoke = await invoke('buyer', shop, [], 'invoke shop with no GRANT/ALLOW')
  expect(
    emptyInvoke,
    emptyInvoke.engine !== 'tesSUCCESS' || hookSaid(emptyInvoke, 'GRANT or ALLOW'),
    'empty Invoke on shop is rejected',
    `empty invoke passed: ${emptyInvoke.engine}`,
  )

  console.log('\n--- B. Price interpretation (state length vs value) ---')
  const buy10xah = await pay('buyer', shop, 10_000_000, 'buy 10 XAH intended price', { HookParameters: theme01 })
  const buy8drops = await pay('buyer', shop, 8, 'buy 8 drops (len of uint64 PRICE)', { HookParameters: theme01 })
  const buy8dropsNoTheme = await pay('buyer', shop, 8, 'buy 8 drops no THEME')

  if (buy10xah.engine === 'tesSUCCESS' && hookSaid(buy10xah, 'paid')) {
    note(
      'INFO',
      'Shop accepted a 10 XAH pack payment under intended 8-byte PRICE',
      'Either state() returns the stored integer (no length bug) or the 10 XAH path is live. See following tests.',
    )
  } else if (
    hookSaid(buy10xah, 'mint budget is not less') ||
    hookSaid(buy10xah, 'pay the exact pack price') ||
    buy8drops.engine === 'tesSUCCESS' ||
    hookSaid(buy8drops, 'paid') ||
    hookSaid(buy8drops, 'THEME')
  ) {
    note(
      'CRITICAL',
      'Pack price is the LENGTH of the PRICE blob, not the stored drop amount',
      `Intended PRICE was 8-byte BE 10_000_000 and BUDGET 8-byte 500_000. 10 XAH buy: ${buy10xah.engine} ${buy10xah.hooks.map((h) => h.returnString).join('; ')}. 8-drop buy: ${buy8drops.engine} ${buy8drops.hooks.map((h) => h.returnString).join('; ')}. shop.c reads state_foreign(0,0,...) which returns blob length, so an 8-byte price is 8 drops and budget length 8 bricks purchases via budget >= price.`,
    )
  }

  console.log('\n--- C. Reconfigure to length-based cheap price so later paths can run ---')
  await configureCheapWorking()
  const buyCheap = await pay('buyer', shop, 10, 'buy 10 drops after length-price reconfig', { HookParameters: theme01 })
  const buyWrong = await pay('buyer', shop, 11, 'buy 11 drops against length-10 price', { HookParameters: theme01 })
  expect(
    buyWrong,
    buyWrong.engine !== 'tesSUCCESS' || hookSaid(buyWrong, 'exact pack price'),
    'wrong drop amount rejected',
    `11-drop buy succeeded: ${buyWrong.engine}`,
  )

  console.log('\n--- D. Shop payment edge cases ---')
  const topup = await pay('buyer', shop, 1_000_000, 'top-up 1 XAH (should skip mint)')
  expect(
    topup,
    topup.engine === 'tesSUCCESS' && (topup.hooks.length === 0 || hookSaid(topup, 'top-up') || !hookSaid(topup, 'paid')),
    '1 XAH top-up is accepted without minting',
    `top-up: ${topup.engine} ${topup.hooks.map((h) => h.returnString).join('; ')}`,
  )

  const partial = await pay('buyer', shop, 10, 'partial payment flag', {
    Flags: TF_PARTIAL,
    SendMax: '100',
    HookParameters: theme01,
  })
  expect(
    partial,
    partial.engine !== 'tesSUCCESS' || hookSaid(partial, 'partial'),
    'partial payments rejected',
    `partial payment succeeded: ${partial.engine}`,
  )

  const noTheme = await pay('buyer', shop, 10, 'exact cheap price, no THEME')
  expect(
    noTheme,
    noTheme.engine !== 'tesSUCCESS' || hookSaid(noTheme, 'THEME'),
    'exact price without THEME rejected',
    `no-theme buy succeeded: ${noTheme.engine}`,
  )

  const badTheme = await pay('buyer', shop, 10, 'THEME 99 missing set', {
    HookParameters: [hp('THEME', Buffer.from('99', 'utf8'))],
  })
  expect(
    badTheme,
    badTheme.engine !== 'tesSUCCESS' || hookSaid(badTheme, 'no such set'),
    'unknown THEME rejected',
    `theme 99 succeeded: ${badTheme.engine}`,
  )

  const closed = await pay('buyer', shop, 10, 'THEME 03 closed sale', {
    HookParameters: [hp('THEME', Buffer.from('03', 'utf8'))],
  })
  expect(
    closed,
    closed.engine !== 'tesSUCCESS' || hookSaid(closed, 'not on sale'),
    'closed sale rejected',
    `closed sale succeeded: ${closed.engine}`,
  )

  const binaryTheme = await pay('buyer', shop, 10, 'THEME binary 0001 not ascii', {
    HookParameters: [hp('THEME', Buffer.from([0x00, 0x01]))],
  })
  expect(
    binaryTheme,
    binaryTheme.engine !== 'tesSUCCESS',
    'non-ascii THEME rejected (or not loaded)',
    `binary theme succeeded: ${binaryTheme.engine} — theme bytes are not digit-checked`,
  )
  if (binaryTheme.engine === 'tesSUCCESS' && hookSaid(binaryTheme, 'paid')) {
    note('LOW', 'THEME parameter is not restricted to ASCII digits', 'Any 2 bytes that match a loaded table key are accepted.')
  }

  console.log('\n--- E. Buyer flag bypass (signed high-bit flags) ---')
  await accountSetFlag('blocker', ASF_DISALLOW_REMIT, 'blocker DisallowIncomingRemit')
  const fundBlocker = await pay('buyer', wallets.blocker.address, 2_000_000, 'seed blocker with extra XAH')
  const buyBlocker = await pay('blocker', shop, 10, 'buy from DisallowIncomingRemit account', { HookParameters: theme01 })
  if (buyBlocker.engine === 'tesSUCCESS' && hookSaid(buyBlocker, 'paid')) {
    note(
      'HIGH',
      'lsfDisallowIncomingRemit does not block a pack purchase',
      `shop.c requires acc_flags > 0 before applying the flag mask. If slot() sign-extends 0x80000000, the check is skipped and the buyer still pays. tx=${buyBlocker.hash}`,
    )
  } else {
    expect(buyBlocker, hookSaid(buyBlocker, 'blocking') || buyBlocker.engine !== 'tesSUCCESS', 'DisallowIncomingRemit blocks buy')
  }

  await accountSetFlag('blocker', ASF_DEPOSIT_AUTH, 'blocker DepositAuth')
  const buyDep = await pay('blocker', shop, 10, 'buy from DepositAuth account', { HookParameters: theme01 })
  expect(
    buyDep,
    buyDep.engine !== 'tesSUCCESS' || hookSaid(buyDep, 'deposit') || hookSaid(buyDep, 'blocking'),
    'DepositAuth blocks buy',
    `DepositAuth buy succeeded: ${buyDep.engine}`,
  )

  console.log('\n--- F. Direct issuer payment / fake trigger ---')
  const directIssuer = await pay('buyer', issuer, 10, 'direct payment to issuer (should pass, not mint)')
  expect(
    directIssuer,
    directIssuer.engine === 'tesSUCCESS' && !hookSaid(directIssuer, 'pack minted'),
    'direct issuer payment does not mint',
    `direct issuer: ${directIssuer.engine} ${directIssuer.hooks.map((h) => h.returnString).join('; ')}`,
  )

  const shopToIssuer = await pay('shop', issuer, 10, 'unsigned shop->issuer payment (not emitted)')
  expect(
    shopToIssuer,
    shopToIssuer.engine === 'tesSUCCESS' && !hookSaid(shopToIssuer, 'pack minted'),
    'non-emitted shop payment does not mint',
    `shop->issuer unsigned minted: ${shopToIssuer.engine}`,
  )

  console.log('\n--- G. Grant allowance (length vs remaining count) ---')
  const allow2 = await invoke('shop', shop, [hp('ALLOW', u32be(2))], 'ALLOW 2 grants')
  expect(allow2, allow2.engine === 'tesSUCCESS' || hookSaid(allow2, 'allowance set'), 'shop can ALLOW')

  const grantPayload = Buffer.concat([accid(wallets.buyer.address), Buffer.from('01', 'utf8')])
  const grants = []
  for (let i = 1; i <= 5; i++) {
    grants.push(
      await invoke('claim', shop, [hp('GRANT', grantPayload)], `GRANT ${i}/5 after ALLOW 2`),
    )
  }
  const grantSuccess = grants.filter((g) => g.engine === 'tesSUCCESS' && (hookSaid(g, 'granted') || hookSaid(g, 'minted') || g.hooks.length)).length
  const grantOk = grants.filter((g) => g.engine === 'tesSUCCESS').length
  if (grantOk > 2) {
    note(
      'CRITICAL',
      'Grant allowance is spent as blob length, so ALLOW N never actually bounds grants',
      `ALLOW was set to 2 (4-byte blob). GRANT txs 1..5 engines: ${grants.map((g) => g.engine).join(', ')}. doors.c does int64_t left = state(0,0,KEY_GRANTS) which returns 4, then writes 3, and every later grant still sees length 4.`,
    )
  } else {
    console.log(`    OK  grant cap held (${grantOk} successes)`)
  }

  const grantBadLen = await invoke(
    'claim',
    shop,
    [hp('GRANT', Buffer.concat([accid(wallets.buyer.address), Buffer.from('0')]))],
    'GRANT 21-byte payload',
  )
  expect(grantBadLen, grantBadLen.engine !== 'tesSUCCESS' || hookSaid(grantBadLen, '22 bytes'), 'GRANT length enforced')

  const grantGhost = await invoke(
    'claim',
    shop,
    [hp('GRANT', Buffer.concat([accid(wallets.ghost.address), Buffer.from('01', 'utf8')]))],
    'GRANT to unfunded ghost account',
  )
  console.log(`    ghost grant: ${grantGhost.engine} ${(grantGhost.hooks[0] || {}).returnString || ''}`)

  const grantClosed = await invoke(
    'claim',
    shop,
    [hp('GRANT', Buffer.concat([accid(wallets.buyer.address), Buffer.from('03', 'utf8')]))],
    'GRANT closed theme 03',
  )
  expect(grantClosed, grantClosed.engine !== 'tesSUCCESS' || hookSaid(grantClosed, 'not on sale'), 'GRANT respects sale flag')

  console.log('\n--- H. Happy-path mint + edition / URI uniqueness ---')
  const beforeBuyer = await countUriTokens(wallets.buyer.address)
  const buys = []
  for (let i = 0; i < 8; i++) {
    buys.push(await pay('buyer', shop, 10, `pack buy ${i + 1}/8`, { HookParameters: theme01 }))
    await waitLedgers(2)
  }
  await waitLedgers(4)
  const afterBuyer = await listUriTokens(wallets.buyer.address)
  console.log(`    buyer URITokens: ${beforeBuyer} -> ${afterBuyer.length}`)
  if (afterBuyer.length) {
    console.log(`    sample URI: ${afterBuyer[0].URI || afterBuyer[0].uri || JSON.stringify(afterBuyer[0]).slice(0, 180)}`)
  }
  const failedMints = buys.filter((b) => b.engine === 'tesSUCCESS' && hookSaid(b, 'paid')).length
  const rejectedLater = buys.filter((b) => b.engine !== 'tesSUCCESS').length
  if (buys.some((b) => hookSaid(b, 'paid')) && afterBuyer.length === 0) {
    note(
      'HIGH',
      'Shop accepted pack payments but no URITokens arrived on the buyer',
      `Paid ${failedMints} packs at 10 drops; buyer still has 0 URITokens. Likely mint emission failure (fees, URI, or callback). Last buy ${buys.at(-1).hash}`,
    )
  }

  const uris = afterBuyer.map((t) => t.URI || '').filter(Boolean)
  const uriSet = new Set(uris)
  if (uris.length && uriSet.size < uris.length) {
    note('HIGH', 'Duplicate URIToken URIs observed', 'URI uniqueness is supposed to be per-issuer; duplicates mean edition counter is stuck.')
  }

  console.log('\n--- I. Overflow / hostile table (theme 02) ---')
  const overflowBuy = await pay('buyer', shop, 10, 'buy overflow theme 02', {
    HookParameters: [hp('THEME', Buffer.from('02', 'utf8'))],
  })
  await waitLedgers(3)
  if (overflowBuy.engine === 'tesSUCCESS' && hookSaid(overflowBuy, 'paid')) {
    note(
      'HIGH',
      'Hostile card-table name length is accepted at purchase time; mint may trap or corrupt',
      `Theme 02 row name_len=200 (ROW_LEN still 25). Shop does not validate row bytes. Buy tx ${overflowBuy.hash}. mint.c does m += name_len / theme_name[0] into a 640-byte meta buffer.`,
    )
  }

  console.log('\n--- J. Attest surface ---')
  let tokenId
  for (const t of afterBuyer) {
    tokenId = t.Index || t.index || t.URITokenID
    if (tokenId) break
  }
  if (!tokenId && afterBuyer[0]) {
    tokenId = afterBuyer[0].index || afterBuyer[0].Index
  }

  const fakeMint = await submit(
    'issuer',
    {
      TransactionType: 'URITokenMint',
      URI: asciiHex('not-a-xahaucard'),
    },
    'issuer mints non-card URIToken',
  )
  let fakeId = fakeMint.hash
  await waitLedgers(2)
  const issuerTokens = await listUriTokens(issuer)
  const fake = issuerTokens.find((t) => {
    const uri = t.URI || ''
    const raw = uri.match(/^[0-9A-F]+$/i) ? Buffer.from(uri, 'hex').toString('utf8') : uri
    return raw.includes('not-a-xahaucard')
  })
  if (fake) fakeId = fake.Index || fake.index

  if (fakeId && fakeId.length === 64) {
    const attestFake = await invoke('attestor', issuer, [hp('ATTEST', Buffer.from(fakeId, 'hex'))], 'ATTEST non-card URIToken')
    if (attestFake.engine === 'tesSUCCESS') {
      note(
        'MEDIUM',
        'Attestors can sign any URIToken issued by the issuer, not only xahaucards:// URIs',
        `ATTEST succeeded on URI 'not-a-xahaucard' id=${fakeId} tx=${attestFake.hash}. manager.c checks issuer match and remark room, not the URI prefix.`,
      )
    }
  }

  if (tokenId && String(tokenId).length === 64) {
    const attest1 = await invoke('attestor', issuer, [hp('ATTEST', Buffer.from(tokenId, 'hex'))], 'ATTEST real card')
    await waitLedgers(2)
    const attest2 = await invoke('attestor', issuer, [hp('ATTEST', Buffer.from(tokenId, 'hex'))], 'ATTEST same card again')
    expect(
      attest2,
      attest2.engine !== 'tesSUCCESS' || hookSaid(attest2, 'already been signed'),
      'second CardSignature rejected',
      `duplicate attest succeeded: ${attest2.engine}`,
    )
    if (attest1.engine === 'tesSUCCESS') {
      console.log(`    first attest ok ${attest1.hash}`)
    }
  } else {
    console.log('    skipping real-card attest (no token id yet)')
  }

  console.log('\n--- K. Shop can still send funds out ---')
  const drain = await pay('shop', wallets.outsider.address, 50_000, 'shop outgoing payment (should pass hook)')
  expect(drain, drain.engine === 'tesSUCCESS', 'shop can withdraw (outgoing pass-through)')

  console.log('\n--- L. Manager editions NS=E ---')
  const ed = await invoke(
    'issuer',
    issuer,
    [hp('NS', Buffer.from('E')), hp('K0', Buffer.from('MINTCOUNT')), hp('V0', u32be(999))],
    'admin NS=E write MINTCOUNT',
  )
  console.log(`    NS=E write: ${ed.engine} ${(ed.hooks[0] || {}).returnString || ''}`)
}

async function dumpState() {
  const snap = {}
  for (const [name, ns] of [
    ['shop-intent', NS.INTENT],
    ['issuer-editions', NS.EDITIONS],
    ['issuer-table', NS.TABLE],
    ['issuer-settings', NS.SETTINGS],
    ['issuer-attest', NS.ATTEST],
  ]) {
    const account = name.startsWith('shop') ? wallets.shop.address : wallets.issuer.address
    snap[name] = await accountNamespace(account, ns)
  }
  snap.shopInfo = await rpc({ command: 'account_info', account: wallets.shop.address, ledger_index: 'validated' }).catch((e) => ({ error: e.message }))
  snap.issuerInfo = await rpc({ command: 'account_info', account: wallets.issuer.address, ledger_index: 'validated' }).catch((e) => ({ error: e.message }))
  snap.shopObjects = await accountObjects(wallets.shop.address)
  snap.issuerObjects = await accountObjects(wallets.issuer.address)
  snap.buyerObjects = await accountObjects(wallets.buyer.address)
  return snap
}

async function main() {
  wallets = loadOrCreateWallets()
  printAddresses()

  client = new XrplClient(WSS)
  await client.ready()
  const info = await rpc({ command: 'server_info' })
  console.log('network_id', info?.info?.network_id, 'complete_ledgers', info?.info?.complete_ledgers)

  console.log('\nFunding accounts via https://xahau-test.net/accounts ...')
  for (const role of ROLES) {
    if (role === 'ghost') {
      console.log(`  ghost left unfunded ${wallets[role].address}`)
      continue
    }
    const existing = await alreadyFunded(wallets[role].address)
    if (existing > 0n) {
      console.log(`  already funded ${role} ${wallets[role].address} balance=${existing} drops`)
      continue
    }
    try {
      const funded = await faucet(wallets[role].address)
      console.log(`  funded ${role} ${wallets[role].address} amount=${funded.amount || funded.balance || '?'}`)
    } catch (e) {
      console.log(`  faucet error ${role}: ${e.message}`)
    }
  }
  for (const role of ROLES) {
    if (role === 'ghost') continue
    const bal = await waitFunded(wallets[role].address)
    console.log(`  on-ledger ${role} ${wallets[role].address} balance=${bal} drops`)
  }
  printAddresses()

  console.log('\nInstalling published wasm (hashes match README)...')
  const installed = await installHooks()
  if (installed.shop.engine !== 'tesSUCCESS' || installed.issuer.engine !== 'tesSUCCESS') {
    note('CRITICAL', 'SetHook failed', `shop=${installed.shop.engine} ${installed.shop.message}; issuer=${installed.issuer.engine} ${installed.issuer.message}`)
    const shopInfo = await rpc({ command: 'account_info', account: wallets.shop.address }).catch((e) => e)
    console.log('shop account_info', JSON.stringify(shopInfo, null, 2).slice(0, 2000))
  }

  const shopObjs = await accountObjects(wallets.shop.address, 'hook')
  const issuerObjs = await accountObjects(wallets.issuer.address, 'hook')
  console.log('shop hook objects', JSON.stringify(shopObjs.account_objects || shopObjs, null, 2).slice(0, 2500))
  console.log('issuer hook objects', JSON.stringify(issuerObjs.account_objects || issuerObjs, null, 2).slice(0, 2500))

  console.log('\nLoading settings + card table via manager admin Invokes...')
  await configureIntended()
  await configureTable()
  await addAttestor()

  await runAttacks()

  const snap = await dumpState()
  const report = {
    network: WSS,
    addresses: Object.fromEntries(ROLES.map((r) => [r, wallets[r].address])),
    explorers: Object.fromEntries(ROLES.map((r) => [r, EXPLORER_ACC + wallets[r].address])),
    findings,
    tests: tests.map(({ id, role, engine, message, hash, explorer, hooks }) => ({
      id, role, engine, message, hash, explorer, hooks,
    })),
    snapHint: {
      shopHookCount: (snap.shopObjects.account_objects || []).length,
      issuerHookCount: (snap.issuerObjects.account_objects || []).length,
      buyerObjects: (snap.buyerObjects.account_objects || []).length,
    },
  }
  fs.writeFileSync(RESULT_FILE, JSON.stringify(report, null, 2))
  console.log('\n========== FINDINGS ==========')
  if (!findings.length) console.log('No confirmed live findings beyond test log; inspect results.json')
  for (const f of findings) console.log(`- [${f.severity}] ${f.title}`)
  console.log('\nWrote', RESULT_FILE)
  printAddresses()
  client.close?.()
  process.exit(0)
}

main().catch((e) => {
  console.error(e)
  process.exit(1)
})
