# XahauCards hooks — live testnet audit

Audit of [XRPLWin/xahaucards-hooks](https://github.com/XRPLWin/xahaucards-hooks) on **Xahau testnet** (`wss://xahau-test.net`, network id **21338**).

The four published wasm files were installed as-is. File SHA-256 matched the repo README:

| file | sha256 |
| --- | --- |
| `shop.wasm` | `428610602ba0666cf90ce181537586e65b3990d314483c9aff49af7fbbd51c10` |
| `doors.wasm` | `40d04eafa7c8aa3d8ae54dd7c4006c8a5008d8413e9f5a3336ea424592343503` |
| `mint.wasm` | `9fbbca1286faf17d591f6616a291fe91d27bd0009d3ccb58652b754791f46d30` |
| `manager.wasm` | `1c73661f563cb8d5a8f16b4bb865fca980d26d471ced0ce5df0f6d097bdbdfb3` |

On-ledger `HookHash` is SHA-512-half of the wasm, not SHA-256, so the hashes below differ from the table above. That is expected.

Harness: `amendment-tests/xahaucards-audit/` (`node audit.js`, then `node followup.js`).

---

## Public addresses

| Role | Address | Explorer |
| --- | --- | --- |
| **shop** (`shop.wasm` + `doors.wasm`) | `rfttwgbFPKLSuD2yf2NGTk4qhnZMozGCbp` | [account](https://test.xahauexplorer.com/en/account/rfttwgbFPKLSuD2yf2NGTk4qhnZMozGCbp) |
| **issuer** (`mint.wasm` + `manager.wasm`) | `rss6oKBRfJAyJFHQYG7VPfAtuam9cbbpWn` | [account](https://test.xahauexplorer.com/en/account/rss6oKBRfJAyJFHQYG7VPfAtuam9cbbpWn) |
| **buyer** | `refuGbntXXeBMGJ7Eym9p1hb2pLWVqBBP` | [account](https://test.xahauexplorer.com/en/account/refuGbntXXeBMGJ7Eym9p1hb2pLWVqBBP) |
| **claim** (grant authority) | `rn6ybgQjNsBkb2SU9vydYhQgiATPNr1RXd` | [account](https://test.xahauexplorer.com/en/account/rn6ybgQjNsBkb2SU9vydYhQgiATPNr1RXd) |
| **attestor** | `rErfEEqJph8VQMJAF3gw7PPsfx9452RK3v` | [account](https://test.xahauexplorer.com/en/account/rErfEEqJph8VQMJAF3gw7PPsfx9452RK3v) |
| **outsider** | `rpPEyJYwqvfCXDYJH2RDsuj4eixe5nS6BY` | [account](https://test.xahauexplorer.com/en/account/rpPEyJYwqvfCXDYJH2RDsuj4eixe5nS6BY) |
| **blocker** | `rBnvCdpT7FHHCZj7Pm7idNgUtdRzvc6W9K` | [account](https://test.xahauexplorer.com/en/account/rBnvCdpT7FHHCZj7Pm7idNgUtdRzvc6W9K) |
| **ghost** (unfunded until grant) | `r4t5kTe2YNNeX8aDNCLzWpAt8mX4k7GApb` | [account](https://test.xahauexplorer.com/en/account/r4t5kTe2YNNeX8aDNCLzWpAt8mX4k7GApb) |

Install transactions:

- shop: [265E91F424C2CA8CFA6D42DBCD1D21ADBD14CFD40EF0EA73E0EA9CEB17A4F4C5](https://test.xahauexplorer.com/explorer/265E91F424C2CA8CFA6D42DBCD1D21ADBD14CFD40EF0EA73E0EA9CEB17A4F4C5)
- issuer: [D1E6184C44DBD2C743B0F69AC328898D2EDA1604C770F0B5D4F6132B8E4B5A76](https://test.xahauexplorer.com/explorer/D1E6184C44DBD2C743B0F69AC328898D2EDA1604C770F0B5D4F6132B8E4B5A76)

On-ledger hook hashes:

| hook | HookHash |
| --- | --- |
| shop | `7FD19A1E166C88101F38E3049DC78B86F1D44DC960D8B46FDEA34286F9912E00` |
| doors | `C16AC4E5A655D7FB32C8E3070036EE2D6DBAEA6A6F84DA1B15DD8140F5D8647D` |
| mint | `C5412323C223BA7F45CAF67D18EDB120366927EB058619910FA6EC65174435F6` |
| manager | `D58B91077361FF1FA326F1A374B2AC0CBF57140DCEEC932DE53660EF8CCEB00E` |

---

## What did not break

These checks rejected as intended (`tecHOOK_REJECTED` unless noted):

- Outsider cannot write settings, `ALLOW`, `GRANT`, or `ATTEST`
- Exact pack price required; missing / unknown / closed `THEME` rejected
- `lsfDisallowIncomingRemit` and `lsfDepositAuth` block buys
- Direct (non-emitted) payments to the issuer do **not** mint
- `ALLOW 2` then five `GRANT`s: only two succeeded; the third was `the grant allowance is used up`
- Duplicate `CardSignature` rejected
- Partial native payment died at protocol (`temBAD_SEND_NATIVE_MAX`) before the hook
- Happy path minted packs (buyer accumulated dozens of `xahaucards://XC-…` URITokens; editions increment `…-1`, `…-2`, `…-3`)

`state(0,0)` / `state_foreign(0,0)` is **not** a length oracle on this network. An 8-byte big-endian `PRICE` of 10 XAH was charged as 10 XAH, and a 4-byte grant counter decremented correctly. A 10-byte `PRICE` blob made the shop report `pack price is not configured` (decode fails for blobs longer than 8 bytes).

Mint does not check the trigger amount or intent status; it only requires an emitted Payment from the configured shop with a 23-byte intent. That is consistent with the shop being the gate. User Payments cannot forge `EmitDetails`.

---

## Findings proven on ledger

### 1. Attestors can sign any URIToken from this issuer (integrity)

**Severity:** medium  
**Where:** `src/manager.c` `attest()`

`attest()` checks issuer match, remark room, and URI **length**. It does **not** require a `xahaucards://` prefix or an `XC-` card code.

We minted `http://evil.example/this-is-not-a-xahaucard-token-xxxx` from the issuer and ATTEST’d it. The hook accepted and wrote `CardSignature` plus a sliced `meta.1` image.

| step | result | tx |
| --- | --- | --- |
| Fake URITokenMint | `tesSUCCESS` | [B0F5FD21…](https://test.xahauexplorer.com/explorer/B0F5FD21C0E9176D4A88B1F7D3DA27F1E1CC8B4D63A9A6F2FC5D686E609DBAFD) |
| ATTEST of that token | `tesSUCCESS` — `xahaucards: card signed by rErfEEqJph8VQMJAF3gw7PPsfx9452RK3v` | [65AC68F5…](https://test.xahauexplorer.com/explorer/65AC68F5FDC0DD097C129471F971B385BC3E653C9979D37B67D1D72279D47D4B) |

Token `B46FD9279D69953ADB2C5C58890D3C25DD7043CEEFF6A4E55766F106C466340B` now carries `CardSignature` and image `https://xahaucards.com/card/xample/this-is-no-01.avif` (17 bytes sliced from URI offset 13, plus the attestor slot).

A leaked attestor key, or the issuer minting junk, can bless non-cards. Short URIs are rejected (`URI is too short`); long ones are not.

**Fix:** require `xahaucards://` (or the 17-byte `XC-` code) before signing.

---

### 2. Card-table lengths are not bounded; hostile rows mint corrupt metadata

**Severity:** medium  
**Where:** `src/mint.c` metadata assembly; shop does not validate row bytes

Shop does not inspect subject rows. Theme `02` was loaded with `name_len = 200` on a 25-byte row. The shop still took payment and mint still emitted five cards.

- Overflow buy: [5B71F95A…](https://test.xahauexplorer.com/explorer/5B71F95AB5A4D2BBE76C9C654D75CFFD48E0FB26D7714FB3BCA276AE3A722112) — `paid — your cards are being minted`
- On-ledger `meta.0` name is `AAAAAAAAAAAAAAAAAAAAALPH` plus embedded NULs (malformed JSON), e.g. token `491186BC5E6E7355016177F3D94C2B57832111E4D6A38F19C3C2BEFCE6F0DCA9`, URI `xahaucards://XC-02-001-L-20-20-1`

Cause: `mint.c` uses `BLIT64` (a 64-byte copy) and then `m += name_len` / `theme_name[0]` with no cap against `NAME_WIDTH` or `meta[640]`. This needs the issuer’s admin path, but a bad or hostile table does not fail closed — buyers still pay.

Stacked huge length bytes (name + theme + faction + rarity) may also walk off `meta[640]`; that worst case was not crashed in this run, but one hostile row already produced garbage JSON on ledger.

**Fix:** cap every table-derived length before copy / pointer advance; reject rows that are not exactly `ROW_LEN` with `name_len <= NAME_WIDTH`.

---

### 3. No refund if mint does not deliver (design, still a money-flow hole)

**Severity:** medium (economic / custody)  
**Where:** `src/shop.c`, `src/trigger.h` `cbak()`

Shop `cbak` on trigger success **deletes** the intent; on failure it marks `F` / `L`. It never pays the buyer back.

The 10 XAH pack [57A855D1…](https://test.xahauexplorer.com/explorer/57A855D1F8507B32D7B91F2EB1A342909946219E3081B893244AF4866C7EEC7D) forwarded only the 0.5 XAH budget to the issuer; the rest stayed on the shop. The shop can also send funds out ([9C4B0930…](https://test.xahauexplorer.com/explorer/9C4B0930E3D8D20E369D2D5764D96A4E2F1D0FFBD1088BF8AFD828603FF710F5)).

If remarks/remits fail after the shop has already `DONE`, the buyer is unsecured. Issuer emission fees also come from the issuer balance, not only from the forwarded budget.

**Fix:** decide whether a failed mint should refund; if not, document that the shop is a hot wallet, not an escrow.

---

### 4. 1 XAH “top-up” is indistinguishable from a mistaken pack payment

**Severity:** low  
**Where:** `src/shop.c` (`TOPUP_DROPS == 1000000`)

`drops == 1_000_000` is accepted with no `THEME` and no mint:

[3890C982…](https://test.xahauexplorer.com/explorer/3890C982C4D080781D059C32372709AD492F8E66CD2EEA72E0FB8801138EE8B8)

Intended for operator top-ups; a user who pays exactly 1 XAH donates.

**Fix:** require an explicit top-up parameter, or use a destination tag / Invoke for operator funding.

---

### 5. Pack RNG is `sha512half(parent_txid || ledger_last_hash)`

**Severity:** info / low  
**Where:** `src/mint.c`

Not ground against this UNL in the time available, but a validator that can influence `ledger_last_hash` can bias rarity and stats. Normal for this construction; not a user-level replay.

---

## Auth and economics that held

| Test | Result | Tx |
| --- | --- | --- |
| Outsider admin | `must carry ATTEST, or be signed by the issuer` | [D15EE3E6…](https://test.xahauexplorer.com/explorer/D15EE3E67703503B4726A377A041C4B1A0E9373CB936617FD600AF9698C54406) |
| Outsider ALLOW | `only the shop itself may set the grant allowance` | [4BF149DD…](https://test.xahauexplorer.com/explorer/4BF149DDFD363392FA27CEF94AD98BCC80A22472FF82787F73C3D1CFE2D58B85) |
| GRANT cap | 2 of 5 after `ALLOW 2` | success [8E0E0258…](https://test.xahauexplorer.com/explorer/8E0E02582D0A8685972D2E3CBB6F13CDE1D672D64EE246056178F1D1BB073195) / reject [DF3F7B66…](https://test.xahauexplorer.com/explorer/DF3F7B660A02BC60359202D15D413C9F7090348580C1598F2DA83E46AF3877FD) |
| GRANT to new account | ghost funded + 5 cards | [4F7F54D8…](https://test.xahauexplorer.com/explorer/4F7F54D89E2C6AF7BC5770E3F64826316D9D916B4C6ACB478EE06C0ED7E6D4A5) |
| Duplicate attest | `already been signed` | [1396FBF3…](https://test.xahauexplorer.com/explorer/1396FBF32FBD1CE766E033381F42858C0AE5F9C3488ADD84CEF4496BF8480E6E) |
| Closed sale | `that set is not on sale` | [D0352E64…](https://test.xahauexplorer.com/explorer/D0352E64ED43B3CF58E9049282FB19EF1D92E497C49AFF763B6B68BA426EBFDA) |
| DisallowIncomingRemit | blocked at shop | [B9C40818…](https://test.xahauexplorer.com/explorer/B9C40818B09852F6AC26EDA794BE249048074A8D14163B4F5F33F66E13301D44) |
| DepositAuth | blocked at shop | [7DC04E97…](https://test.xahauexplorer.com/explorer/7DC04E976ED8EB58351A2FA8F8FA7BFABD0E2E165EF9873C33EEF9384187E484) |
| Real card attest | `card signed by rErfEE…` | [C95D70E8…](https://test.xahauexplorer.com/explorer/C95D70E8E1EAC4E5190571C6D355A10BB17B7812D70CE2753011512458B4AEC0) |

`THEME` is not digit-checked; binary `0x00 0x01` was rejected as `no such set` (table miss), not as invalid digits.

Manager `NS=E` write of `MINTCOUNT` failed with `could not write the entry` ([32DEF9B4…](https://test.xahauexplorer.com/explorer/32DEF9B4D1F47406DE08A9B52DB10739EF95F9A08BB0821496EC964B31D510F9)). Operational footgun, not an outsider exploit.

---

## Bottom line

We could not steal XAH or bypass payment / allowance / auth as an outsider.

We did break the “signature ⇒ real card” invariant, and showed that a hostile card table mints paid packs with garbage metadata and no rollback.

Highest-impact code fixes:

1. Require `xahaucards://` (or the 17-byte `XC-` code) in `attest()`.
2. Cap every table length before `BLIT64` / `m +=`.
3. Decide whether a failed mint should refund.

---

## How to re-run

```bash
cd amendment-tests/xahaucards-audit
npm install
node audit.js      # fund / install / first attack pass
node followup.js   # second pass on the same accounts
```

`wallets.json` is reused across runs. The testnet faucet allows about one new account per minute. Raw logs: `results.json`, `followup-results.json`.
