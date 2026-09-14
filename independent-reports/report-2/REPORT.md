# XahauCards hook review — findings report

**Prepared for:** XRPLWin
**Prepared by:** Kairo Vault Technologies 合同会社 (independent verification)
**Date:** 2026-09-08
**Tier:** review (agreed 750 USD, RLUSD — received)

---

## What was reviewed

Four hooks across two accounts, at the commit you pinned:

- **Pin:** `95cee8b972310b23c83bc789707be0f83d792ebb` ("Attestors allowance tests", 2026-09-07)
- **Shop account:** `shop.c` (payment gate), `doors.c` (grant / allowance admin)
- **Issuer account:** `mint.c` (roll + mint), `manager.c` (admin + attestor signatures)
- Shared: `namespaces.h` (state contract), `trigger.h` (the emitted trigger + callback)

The source read is `hook/src/` — the tree that builds `hook/build/*.wasm`. Every finding cites `file:line` at that pin. This is a **review, not a formal proof**: each rule reads as **holds**, **fails with a stated input**, or **out of reach of a static read**. Where a chain was needed to settle something, it was checked against live testnet state and against xahaud source rather than left to opinion — and where a claim rests on a source read rather than a run, that is said.

The rules checked are your own: the whitepaper's invariants and the 61 checks in `hook/live-test.mjs`.

## How it was done

1. The state contract (`namespaces.h`) and the trigger/callback (`trigger.h`) were read in full.
2. Each hook was read against its rules.
3. An **adversarial pass** tried to break every "holds" — free packs, forged emissions, grant/attestor/admin bypass, state clobber, roll grinding, double-mint, refund abuse, edition collisions, griefing.
4. The protocol facts the findings depend on were checked against **xahaud source** (`Transactor`, `HookAPI`, `SetRemarks`, `Remit`, `CanonicalTXSet`) and against **live Xahau testnet state** (the deployed issuer's full transaction history and real minted cards).
5. Every candidate finding was verified against the code before it was written down; the draft was then put through an internal red-team and a protocol/calibration panel, and the findings below are what survived.

---

## Summary

This is a mature, carefully-defended codebase — visible hardening from a prior independent audit (the length-byte table overflow; the "sign a junk URIToken" attack), fail-closed defaults throughout, and the roll-fairness residuals documented in the source rather than hidden.

**No critical or high-severity defect was found, and that conclusion held through an adversarial pass.** The output is two questions for you to settle, four low-severity items (all self-inflicted, within a trusted role, or a recoverable delivery-quality residual — none lets an outsider steal funds, mint, or forge), and a set of notes.

This top-line rests on two things stated plainly below and not re-proven here: (a) the emission-forgery bar, which is a read of the xahaud transactor rather than a test we ran, and (b) the deployment bindings (namespaces, `KEY_SHOP` / `ISSUER` / `CLAIM`), which live in the `SetHook` install, not the source.

| Area | Result |
|---|---|
| Payment gate (`shop.c`) | all 8 payment/intent rules **hold** |
| Grant / allowance admin (`doors.c`) | authorization **holds**; the hot key cannot raise its own limit |
| Roll + mint (`mint.c`) | fairness invariants **hold**; 1 edition-key question, 1 annotation-ordering residual |
| Attestor signatures (`manager.c`) | signature + allowance rules **hold** |
| Composition (4 hooks together) | **sound** — namespaces separated, no clobber path |
| Auth / funds (adversarial) | **no bypass** — emission forgery barred in the transactor (source-read) |

---

## Questions for you to settle

Not defects — places where only you can say which reading is authoritative.

### Q1 — Edition numbering is keyed per full card code, not per subject
`mint.c:831/837` key the edition counter by the whole 17-byte code (`CODE_LEN = 17`), i.e. `XC-<theme>-<subject>-<rarity>-<attack>-<health>`, **including the rolled stats**. This is confirmed in source and in live state — the issuer's on-chain edition keys read e.g. `XC-01-061-C-03-04`.

Consequence: the same subject rolled with different stats, or at a different rarity, gets **independent** edition sequences each starting at #1. **We read the whitepaper's numbering as "per subject";** the code keys per full card code. If we've read the whitepaper right, those are two different scarcity models, and the number is sealed into immutable metadata at mint — so it matters which one is intended. Which is authoritative is yours to state; if per-subject is intended, the key length at `mint.c:831/837` would need to cover the subject only.

### Q2 — A non-XAH (IOU) payment to the shop is accepted, not bounced
`shop.c:191-192`: a payment whose `sfAmount` is not native is **accepted** (`tesSUCCESS`), documented as "not XAH, passing… received, not bounced". It records **no** purchase, so the "records no purchase" rule holds. The only open question is whether *receiving* an IOU here (rather than rejecting it) is intended. Your call.

---

## Low-severity findings

None lets an outside party steal funds, mint a pack, or sign as someone else. They are self-inflicted, confined to a trusted role, or a recoverable delivery-quality residual — reported for completeness and hardening.

### L1 — A failed delivery Remit leaves the buyer with nothing and no on-ledger claim
The purchase lifecycle promises "paid → cards, or a recorded claim" (`shop.c:80-86`). There is a window where neither holds:

The issuer's mint applies at L+1 and emits five Remits (the cards) for L+2. The callback keys off the **mint's** result: the mint applied `tesSUCCESS` (it emitted fine), so the callback **deletes** the intent record (`trigger.h`). The five Remits are a separate emission that resolves at L+2 with no callback watching. **If any of those Remits fails at L+2, the intent is already gone — the buyer paid, got that card (or none), and there is no record that anything is owed.**

The clearest trigger is a buyer who passes the receivability check at purchase (`shop.c:338-349`) and then sets `lsfDisallowIncomingRemit` before L+2; `mint.c` does not re-check (confirmed: no flag reference in the file), and a Remit to such an account fails `tecNO_PERMISSION` (confirmed against xahaud `Remit.cpp:294-296`) **atomically — no token is minted anywhere** (the mint is `sb.insert` before `sb.apply`; a `tec` never reaches apply). That case is **self-inflicted** — only the account owner can set the flag, there is no attacker profit — which anchors the severity at low.

But two parts of this are structural, not exotic, and worth fixing regardless of how the L+2 failure arises:
- **The callback comment is wrong.** `trigger.h:316-318` calls this "a card minted to the issuer rather than the buyer — a delivery problem with the card in hand." For an atomic Remit failure (any `tec`, not just this flag) no card is minted at all; it is a total loss, not card-in-hand.
- **The record is deleted before delivery is confirmed.** The intent is removed whenever the mint hook accepts, and nothing watches the L+2 Remits — so *any* L+2 Remit `tec` (issuer momentarily short of reserve, a destination edge case) leaves a paid buyer with no claim record.

Recommend: correct the comment, and have recovery tooling treat "no FAILED record" as *not* proof of delivery. A re-check of the buyer's flags in `mint.c` before minting closes the self-inflicted path outright.

### L2 — Card annotation depends on emitted-transaction apply order (measured to hold; not source-guaranteed)
Per card, `mint.c` emits the metadata SetRemarks **before** the Remit that mints the token (`mint.c:1160-1183` then `:1187-1222`), both from the issuer in the same ledger window, and a SetRemarks on a not-yet-existent token fails `tecNO_TARGET` — which is final (a `tec`, not retried). So a card's metadata depends on the mint applying before its annotation.

**Measured: it does, across your entire deployment.** On the testnet issuer, the full transaction history shows **73 metadata SetRemarks, all `tesSUCCESS`, and 70 mint Remits, all `tesSUCCESS` — zero `tecNO_TARGET`** (and `account_tx` includes failed `tec` transactions, so this is not a "only complete cards are visible" artifact). Directly: in 25 of 25 paired cases, the mint Remit applies at a lower in-ledger index than its SetRemarks. So on this deployment the mint reliably applies first and no card has ever landed un-annotated.

**But we could not establish the ordering as a protocol guarantee from source.** The general rule that orders same-account, sequence-0 *emitted* transactions within a ledger is not one we could pin down — a naive read (ordering by transaction-id hash) would predict intermittent failure, which the on-chain evidence contradicts, so emitted transactions are evidently ordered by a rule we did not locate. It holds in practice; we cannot prove it must.

Recommend cheap insurance rather than reliance: issuer-side tooling that detects any minted URIToken lacking its meta remarks and re-emits the SetRemarks — recoverable, because those remarks are not sealed until written — or a confirmation of the emitted-ordering rule from the xahaud maintainers. Low: never observed to fail, and self-repairing if it ever does.

### L3 — An enrolled attestor can pre-sign any card and repoint its art
`attest()` lets any roll member sign any card this issuer minted (`manager.c:553`). A card takes one signature, first-writer-wins (`manager.c:719-726`), and signing seals `meta.1`'s image URL to the signer's slot (`manager.c:924-956`). A malicious or careless roll member could pre-sign cards to block the intended signer and repoint the art, spending their own SIGS allowance. Bounded by that allowance, requires roll membership (a semi-trusted position by design), cannot move funds, mint, or sign as another attestor. A design tradeoff to be aware of, not an authorization break.

### L4 — Same-ledger duplicate signature over-spends the signer's own allowance
The duplicate-signature scan (`manager.c:648-726`) reads the card's current on-ledger remarks, so two Invokes for the same card in the same ledger both pass it, both decrement the allowance (`manager.c:742-745`), and the second's SetRemarks fails `tecIMMUTABLE` a ledger later. Net: that slot's own allowance drops by two for one signature. Self-harm only, consistent with the documented spend-before-emit stance (`manager.c:730-741`). Your call whether it warrants a guard.

---

## Per-hook — what holds

### shop.c — payment gate (all rules hold)
One state write only — `state_set(intent, txid)` at `shop.c:409`, own namespace, accept path, after every validation check, before the emit; no write on any reject path (a refused purchase records nothing, strongly). Exact price both directions (`:240`); partial-payment rejected with a correct sign-guard (`:181-183`); THEME exactly two bytes (`:255`); set-exists (`:276`) and on-sale (`:304`); buyer receivability checked (`:338-349`); budget-below-price guard (`:369`). Intent keyed by the unique Payment txid. *Note:* THEME digit-ness is enforced only by the table lookup — holds because only `cards:export` writes those keys.

### doors.c — grant + allowance admin (authorization holds)
GRANT gated on sender == issuer `KEY_CLAIM` (`:153-160`), fail-closed if unset; ALLOW gated on sender == the shop account itself (`:352-359`), capped at `GRANTS_MAX`. **The two use different sender checks, so the always-online claim key can spend the allowance but cannot raise it** — a compromised claim authority is bounded to `allowance × ≤10 XAH`. Writes only the shop's own namespace; the removed WITHDRAW door leaves no dangling reads.

### mint.c — roll + mint (fairness holds)
- **Seed / no buyer grind — holds.** `seed = sha512half(parent_txid ‖ ledger_last_hash())` (`:643-648`); `ledger_last_hash()` is the hash of the ledger the buyer's Payment landed in (confirmed against xahaud `HookAPI.cpp`), and the trigger provably executes at L+1, so the reveal hash post-dates the buyer's signature. The buyer-chosen `parent` is useless without it; no reroll path (the callback does not re-emit).
  - **Independently re-derived from public data (free cross-check).** We reproduced a real minted pack end to end from public inputs alone — buyer payment `712EEBCF…`, `ledger_last_hash EAF06F0F…`, and the on-chain shape record — and the five re-derived card codes match the five actually minted on-chain (identical as a set; only the mint order differs). So the roll is a pure, reproducible function of chain data with no operator input — verified from outside the code, not just read from it. Evidence + a runnable script are included (`rederive.mjs`).
  - *Disclosed residual (in your source):* the five-ledger emit window lets whoever *includes* the trigger pick among ≤5 candidate hashes. Quantified: legendary is 2/256 per card, so ~3.85% chance of ≥1 legendary in a five-card pack, which a proposer choosing among five candidate seeds (and willing to burn packs) could lift to ~17.8%. Only a block proposer, only by burning packs, never the buyer — strictly weaker than the payer-grind the design closes. The grant path's authority-resends advantage is bounded by the allowance (each resend mints and delivers a full pack).
- **Rarity strip / stat bands — hold.** `:724-725` partitions 0..255 into 150/70/26/8/2 exactly; `:369-370` give contiguous non-overlapping stat bands, membership enforced by construction, legendary pinned 20/20.
- **Five new tokens to the buyer — holds** (buyer from the intent record, not the trigger sender).
- **Metadata / seal flags — holds**, corroborated on the inspected card (`XC-01-026-C-04-04-1`): meta.0 immutable, meta.1 mutable (open for signing), meta.2 immutable, schema immutable — matching the whitepaper.
- *Notes:* table-sourced strings are copied into card JSON without escaping (`:944`, `:973-977`) — relies on the admin door / loader to reject `"`/`\`; every sellable set must populate all five rarities or a share of packs fail-closed (`:737`).

### manager.c — admin + attestor signatures (rules hold)
Admin gated on `hook_account == sender` (`:125-126`) — the issuer only; this is the `KEY_CLAIM` write-guard doors.c depends on. Signing requires an issuer-written roll entry keyed by the sender (`:574-579`) — no self-enrol. The signature allowance is fail-closed on absent/malformed and decremented only after every check, before the emit (`:611-617`, `:742-745`). `CardSignature` and meta.1 are written immutable; meta.0/meta.2 untouched; the card is not transferred. The junk-URIToken sign attack is closed (`URI_MIN_LEN` + scheme/offset checks + issuer==hook, `:894-905`, `:632-645`).

The full signature lifecycle is corroborated on real cards: an unsigned card carries `meta.1` **mutable**; a signed card (`XC-01-366-R-10-12`) carries `CardSignature` immutable, `meta.1` now **immutable** (sealed by signing), and is owned by a **buyer, not the issuer** — matching the design.

---

## Composition (the four hooks together)

Namespaces are separated by design; `shop.c`/`doors.c` write only `NS_INTENT`, `mint.c` only `NS_EDITIONS`, `manager.c` only issuer namespaces on its own account; no hook writes another's across an account (foreign writes need an `sfHookGrants` entry — confirmed against xahaud — and none is granted but manager's own-account write). By design the four do not co-fire on one transaction (shop/doors answer transactions to the shop; mint/manager answer transactions to the issuer; mint runs on the shop's emitted trigger). The one shared record (the intent) is keyed by the unique txid, so one purchase's record cannot touch another's. No clobber path was found.

---

## Adversarial pass — what was attacked and could not be broken

- **Forge an emission to mint a free pack** — blocked in the transactor (source read): a transaction carrying `sfEmitDetails` without the node's internal emitted flag is rejected (`telNON_LOCAL_EMITTED_TXN`, `Transactor.cpp:127-148`), the network refuses emitted transactions off the wire, and an emitted transaction's sender must equal the emitting hook's own account (`HookAPI.cpp` rule 0) — so no outsider can produce an emission whose sender is the shop. The issuer's gate (sender == `KEY_SHOP` **and** `sfEmitDetails`) therefore only accepts a genuine emission from the shop's own hooks.
- **Underpay / IOU / partial-payment** — blocked at `shop.c` (exact price, partial-flag, IOU-passes-without-purchase).
- **Grant forgery / over-drain** — blocked (claim-key check, allowance floor and ceiling, no counter wrap, 10-XAH cap).
- **Sign without being an attestor / sign a non-card** — blocked (issuer-written roll; URI structure + issuer checks).
- **Admin action by a non-issuer** — blocked (`hook_account == sender`).
- **Clobber another hook's state / forge an intent to claim a refund** — blocked (foreign writes need a grant; a FAILED intent cannot be written without actually paying).
- **Roll grinding by a buyer, double-mint of one payment, edition-collision to strand a buyer, cheap griefing of mints** — all hold (see mint.c and the findings above).

---

## Assumptions accepted, not re-verified

Stated so nothing is passed off as tested that was not:

- The emission-forgery bar (above) is a **read of the xahaud transactor**, not a transaction we submitted. It is the linchpin of "no free pack".
- The callback's delivery decision reads the trigger's engine result (`sfTransactionResult` via `meta_slot`, `trigger.h`); the source comment says this was measured on testnet — we did not re-run it.
- Edition-collision freedom relies on per-transaction hook-state commit within a ledger (standard Xahau behaviour, and what `mint.c:812-821` designs around); accepted, not independently re-run.

---

## Out of scope for this tier / to confirm on the deploy side

1. **Deployment bindings** (not in the source): each hook's installed `HookNamespace` must equal its `NS_*` constant; the `HookCanEmit` grants; the `ISSUER` parameter on the shop hooks and the `SHOP` / `CLAIM` values in issuer settings. Worth a checklist against `deploy.mjs`.
2. **The installed wasm bytes.** This review reads the source that builds them; it does not prove the deployed `hook/build/*.wasm` is that source. Matching the published build hash to the deployed `HookHash` closes that gap — the proof tier does this against the bytes.
3. **The loader's input validation** — several mint-side safeties (JSON-breaking characters, table record widths) are backstops that assume the admin door / loader rejects bad input on the way in.

---

## How to reproduce

- Clone the repo at `95cee8b972310b23c83bc789707be0f83d792ebb`; the reviewed files are under `hook/src/`. Every finding cites `file:line`.
- The on-chain checks read the testnet issuer `rhi2ndQkEmV5x1t7YisEKUF3J8tw4VnJ83` — its full transaction history (SetRemarks / Remit results) and a minted card's remarks.
- Protocol facts cite xahaud (`Transactor.cpp`, `HookAPI.cpp`, `SetRemarks.cpp`, `Remit.cpp`, `CanonicalTXSet.cpp`).

---

*This is a review by the author named above: a written opinion against your stated rules — what holds, what is a question for you, and what a review cannot settle — with the "holds" claims put through an adversarial pass and a protocol/calibration panel, and the chain-dependent ones checked against live state and xahaud source. Where a claim rests on a source read rather than a run, that is said. It is not a formal proof and not a security audit; a proof against the installed wasm bytes, with counterexamples, is the proof tier, available after you've read this.*
