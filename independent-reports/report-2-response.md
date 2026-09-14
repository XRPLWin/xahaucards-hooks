# Response to report-2

Response to [report-2/REPORT.md](report-2/REPORT.md), the hook review by Kairo Vault Technologies
合同会社, dated 2026-09-08. Written 2026-09-08.

The report's own files are published exactly as delivered. Their SHA-256 digests,
as the author gave them, and verified against the copies in `report-2/`:

```
766f5be9e162554918691105a4ce5f9aea64835df585548822429a1b9f4083ca  REPORT.md
88c48d7ae2c0636c0dcf758780df76af21b99911f06a2d25860a09b3a4a411d7  REPORT.html
c8b0552af39fc5d7e6f34e46cad6358efd702ebf5ea646a1cfe812e30c6a521e  REDERIVATION_EVIDENCE.md
1a6887e5436e6d1e5e9a7e3cc8d8776427a96a046ca3f1f5a5231b9038730ee9  rederive.mjs
```

Check them with `sha256sum -c` from inside `report-2/`. Nothing in this response
is in that list: it is a separate file, written here, and the report is not
modified to accommodate it.

## What this review was, and what it was not

Stated first because the wrong summary of it is easy to write and hard to take
back. **A review does not pass or fail, and this one did not "pass".**

What the report says is narrower, and it is what should be quoted:

> The XahauCards hooks (shop, doors, mint, manager) were independently reviewed
> by Kairo Vault Technologies GK in September 2026 at a pinned commit. The review
> found no critical or high severity defect, and that conclusion held through an
> adversarial pass. Two open questions and four low severity items are recorded
> in the review. The review covered source; the installed bytecode and deployment
> bindings were not verified at this tier.


---

Thank you for the work, and particularly for the re-derivation: reproducing a
real pack from public inputs alone is a stronger statement about the roll than
anything a source read could have made, and `rederive.mjs` is now the thing we
would hand anyone who asks whether the odds are real.

Two of the findings turned out to be worth more than their severity suggests,
and one of the two questions found a real disagreement between the code and the
whitepaper that has now been settled in the whitepaper's favour.

| item | status |
| --- | --- |
| Q1 — edition numbering | **fixed** — the hook now keys editions per subject, which is what the whitepaper always promised |
| Q2 — IOU payments accepted | **closed, no change** — unreachable on testnet, and the author agrees it is configuration rather than a hook issue |
| L1 — failed delivery leaves no claim | **partly fixed** — comment corrected, tooling added; the recommended mint-side re-check is deliberately declined |
| L2 — annotation depends on apply order | **fixed** — detection and repair tooling added, and the ordering re-measured over a larger sample |
| L3 — attestor can pre-sign any card | **no change in this build** |
| L4 — same-ledger duplicate signature | **no change in this build** |

One hook changed: `mint.wasm`, for Q1, and its worst-case guard count is
unchanged at 29,555 because a state key's length is a runtime argument rather
than a branch. `shop.wasm`, `doors.wasm` and `manager.wasm` are byte-identical
to the reviewed build — the `trigger.h` edit for L1 is a comment, and it was
written to the same line count on purpose, because `DONE` compiles a `#line`
directive into the return string and a shifted comment would otherwise have
moved two HookHashes for a prose fix.

| hook | HookHash |
| --- | --- |
| shop | `C650B566C37E06ECFB005B6037EA04348FF360FAB166FCBBA673CBCADBE3FBFE` (unchanged) |
| doors | `C16AC4E5A655D7FB32C8E3070036EE2D6DBAEA6A6F84DA1B15DD8140F5D8647D` (unchanged) |
| mint | `29E5097B2BE29D63357F48AA5B6B28F7D48E6475F4C7C94A25E0CADDAF6A55C2` (was `E1055384…`) |
| manager | `E8655E53A5DD40D6783392F01C46171BC1D0FFFD3C48227C1F13F34288FB6EDC` (unchanged) |

---

## Q1 — edition numbering is keyed per full card code

**Fixed. You read the whitepaper right, the whitepaper was right, and the hook
now does what it says.**

The finding is correct, and it is worth being precise about which two documents
disagreed, because it was not the pair the report assumes:

- The **whitepaper** says per subject, in two places — §7 ("#42 is the
  forty-second copy of that subject ever minted") and the card-code caption in
  §2. This is the public promise, and it is the one we have kept.
- The **README** described per-code keying, correctly, at length, and with the
  reserve arithmetic to match. It was accurate about the code and wrong about
  the design.

So the engineering documentation and the product promise had drifted apart, and
the code was following the wrong one. `mint.c` now keys the counter on
`SUBJECT_LEN` — the nine bytes covering `XC-<theme>-<subject>` — rather than the
full seventeen. Every stat pair a subject can be rolled with shares one sequence,
which is what "#42 is the forty-second copy of that subject" requires.

Three consequences worth recording:

- **The reserve falls by about 94%.** The published figures were also stale — the
  sets have grown to 150 / 150 / 100 subjects since, so per-code saturation was
  **7,165 entries, ~1,433 XAH**, not the 5,679 / ~1,136 XAH the README claimed.
  Per subject it is **400 entries, ~80 XAH**. Verified two ways: from the theme
  files against the real rarity ranges in `config/cards.php`, and against the
  ledger, where the issuer's 322 table records decompose exactly as 300 S + 12 F
  + 5 R + 2 T + 2 D + 1 O. README and both deployment guides are corrected.
- **A pack now moves fewer counters than it mints cards, routinely.** Two cards
  of one subject rolled with different stats used to be two codes and two
  counters; they are now one counter moved by two. That was always possible for
  an exactly repeated code and the hook already handles it — the counter is read
  back inside the same execution that wrote it, so the second of a pair takes the
  next number — but it goes from rare to ordinary. The live suite's assertions
  were rewritten accordingly: the invariant is that a pack moves five editions in
  total, that each counter moves by the number of that subject the pack actually
  contained, and that a subject's cards carry exactly the last N numbers their
  counter passed through.
- **It required a fresh issuer, and that is the whole reason it was done now.**
  Against an issuer that has already minted, a restarted counter rebuilds a URI
  that already exists — permanent `tecDUPLICATE`, since the key is derived from
  (issuer, URI). Running `hook/state.mjs` against the existing testnet issuer
  after the change reports its 442 per-code counters as strays, which is exactly
  right and exactly why they cannot be carried over. If a live issuer ever has to
  be migrated rather than replaced, the safe seed is the sum of the per-code
  counters under each subject: the sum is always at least the highest number
  among them, so the next edition issued clears every URI already minted. That
  needs a new admin door and does not exist today.

`mint.c`'s worst-case guard count is unchanged at 29,555 — a state key's length
is a runtime argument, not a branch.

## Q2 — a non-XAH payment to the shop is received, not bounced

**Closed. No change. Accepting is correct, and the case is unreachable anyway.**

Tested on testnet rather than argued, since that is what the question deserved.
`hook/live-test.mjs --only b` provisions a gateway, issues an IOU, and pays the
shop the pack price denominated in it:

```
  pass  Payment to the shop in USD instead of XAH
        tecPATH_DRY  emits 0  "xahaucards: not XAH, passing."
  pass  the IOU payment recorded no purchase  (0)
```

Two layers answer it and the pairing is the point. `shop.c` reaches its length
check, declines to read 48 bytes of IOU as a price, and passes — that is the
hook's half, and `emits 0` is what says no pack was dealt. The ledger's half is
`tecPATH_DRY`: the shop holds no trustline for that currency and never will, so
an IOU cannot be delivered to it whatever any hook returns. The transaction is
refused by the transactor after the hook has already passed it.

Rejecting in the hook would therefore change nothing that can actually happen,
and would cost a guard branch on the hot path every buyer pays for. The case is
pinned in the suite at `hook/live-test.mjs:1887-1945`, with the reasoning above
written out beside the assertion, so a future build that changed this would fail
rather than drift.

**The account settings, since they are the half the review did not read and
anyone can check them on ledger.** Two separate facts, and it is worth keeping
them apart because they stop different things:

| account | `Flags` | trust lines |
| --- | --- | --- |
| shop `rQBBk2D3ekgTM2unwLxgZYMHVX65yYepai` | `2147483648` — `lsfDisallowIncomingRemit` | **0** |
| issuer `raYnSBGqJ3eLDxPoJzBBNPAHrd7mUJxXpW` | `3238526976` — `lsfDepositAuth`, `lsfDisallowIncomingRemit`, `lsfDisallowXRP` | **0** |

It is the **absent trust line** that makes an IOU Payment unroutable, not the
flag: with no line for the currency there is no path, so the transactor answers
`tecPATH_DRY` whatever the hook returned. `lsfDisallowIncomingRemit` is a
separate piece of hardening — it refuses incoming `Remit`s, which is a different
transaction type — and the shop carries it because it issues nothing and is never
a destination for cards.

So Q2 is closed by configuration rather than by code, and it stays closed for
exactly as long as the shop holds no trust line. Both are readable with one
`account_info` and one `account_lines`, which is the point of writing the numbers
down here rather than asserting the conclusion.

The author agreed with this on review: *"not a hook issue, closed by
configuration, and it stays closed as long as the shop keeps that flag and never
sets a trust line."*

## L1 — a failed delivery Remit leaves the buyer with nothing and no claim

**The finding is correct and both structural parts are addressed. The
recommended mint-side flag re-check is declined, with a reason.**

### The comment was wrong, and is fixed

`trigger.h` described an L+2 Remit failure as "a card minted to the issuer
rather than the buyer — a delivery problem with the card in hand." You are right
that this is backwards: the Remit is atomic, so a failure mints nothing
anywhere and the buyer holds nothing at all. The comment now says so.

### "No FAILED record" no longer reads as proof of delivery

The more useful half of this finding is that the wrong belief was not only in a
comment. `hook/state.mjs --intent`, which is the recovery tool an operator
actually runs, printed **"Nothing owed. Every pack so far reached the issuer and
was minted"** on an empty namespace — a claim it is not in a position to make,
for exactly the reason you give. That now says what it can support (no
outstanding claim, every trigger accepted), says plainly that a delivery failing
after the callback leaves no trace in it, and points at the reconciler.

### `hook/deliveries.mjs` — reconciling against the cards themselves

New, and it is the recommendation acted on: recovery tooling that reads the
ledger rather than the intent namespace. A purchase is one chain of
`sfEmitParentTxnID` — the buyer's Payment, the shop's trigger that names it, the
five Remits that name the trigger — and all three legs are visible in the
issuer's own history, so this is one `account_tx` walk and needs no key. A
trigger carrying fewer than five successful Remits is a short pack, printed with
the Payment txid to refund against.

Run against the current testnet deployment: **99 emitted payments from the shop,
98 packs delivered whole, 0 short.** Confirmed independently by a full tally of
the issuer's history — 490 mint Remits, every one `tesSUCCESS`, no failures of
any kind to reconcile.

One note that may be useful to the next reviewer, because it is a trap the
current source cannot warn you about. The ninety-ninth emitted Payment is not a
pack: it is a **takings sweep from an older build**, which passed through this
same path in a Payment wearing the trigger's exact costume — emitted, from the
shop, with real emit details and a parent id. `mint.c:596-607` records that the
sweep and its `SWEEP` marker were both removed with the withdrawal doors, but
the transactions are still in the issuer's history and always will be. The first
cut of this tool duly reported that sweep as five cards that never arrived.
It now discriminates on the mint's own `HookEmitCount` on the trigger — a mint
that rolled cards emitted, a pass emitted nothing — which is true of every build
past and future.

### Why `mint.c` does not re-check the buyer's flags

Declined deliberately, and this is the one recommendation in the report we are
not taking.

`mint.c:52-65` states the invariant: the hook must never roll back for a
buyer-controlled reason, because the pack is fixed the instant the roll happens
and a rollback throws that roll away. A buyer who can force a failure and then
choose when it clears is choosing which ledger hash the next roll reads, and the
seed is public — their own Payment txid and a ledger hash — so they can compute
candidate rolls offline and clear the flag when a good one is due. That is the
grind the whole design closes, arrived at from the other end, and
`trigger.h:369-378` is why the callback does not re-emit either.

Adding the re-check would convert a self-inflicted, no-attacker-profit loss into
a buyer-triggerable regrind. The trade is the wrong way round: the case it
closes requires the buyer to sabotage themselves for nothing, and the door it
opens has a payoff. Left as is, and the loss is now *visible* through
`deliveries.mjs`, which is what was actually missing.

## L2 — card annotation depends on emitted-transaction apply order

**Fixed as recommended: insurance, not reliance.**

We could not establish the ordering rule from source either, and agree it should
not be relied on. `hook/remarks-check.mjs` is the tooling you suggested — it
detects any minted URIToken lacking its meta remarks and writes them.

Re-measured over a larger sample than the report had, on the current testnet
issuer (`rQwpTc4HcZcZKLGzqc6HQxgBDJYmxwvXPk`):

- **490 cards minted, every Remit `tesSUCCESS`.**
- **500 annotation SetRemarks, every one `tesSUCCESS`. Zero `tecNO_TARGET`.**
- **0 un-annotated cards, 0 partially annotated.**

So the ordering still holds, now across 490 cards rather than 70, and still
without a proof that it must.

Settled between us as measured rather than proven, and left in place on that
basis. The author's framing of why insurance is the right shape of answer:
*"if it ever breaks the symptom is a card with a missing annotation, not a lost
card, which is why the note was repair tooling rather than a fix."* That is
exactly the case `remarks-check.mjs --repair` exists for — the token is real, its
URI is correct, and only the remarks are absent, so nothing has to be re-minted
and no edition number moves.

The repair half is the part that needed care, because it writes remarks that
seal immutable and a wrong repair is worse than no repair. Two things guard it:

- **The expected metadata is rebuilt from the issuer's own card table on
  ledger** — the same `NS_TABLE` records and `DOMAIN` setting `mint.c` reads
  through `state_foreign` — and not from `php artisan cards:export`, which is
  the table's source and can have moved on since a card was minted.
- **`--verify` proves the rebuild against cards the hook itself wrote.** It
  re-derives the metadata for every annotated card and diffs it byte for byte.
  Current result: **0 mismatches across 489 cards.** A `--repair` run refuses to
  write anything if that diff is not clean, so a derivation that had drifted
  from `mint.c` is caught on known-good cards rather than discovered by sealing
  something wrong.

The 218 cards reported as "rehosted" are not failures: they were minted under an
earlier `DOMAIN` and keep the host they were minted under. They are classified
apart from mismatches for that reason, and `meta.1` on a signed card is skipped
entirely, since `manager.c` is supposed to have rewritten it.

One incidental correction to the report's method, which may matter if you re-run
it: counting SetRemarks results by engine code alone conflates two unrelated
things. The issuer writes remarks for two reasons — `mint.c` annotating a new
card and `manager.c` recording a signature — and the seven `tecIMMUTABLE`
SetRemarks in this account's history are all of the second kind, signature
writes against cards that already carry one. That is first-writer-wins working,
not an annotation that failed. The tool splits them.

## L3 — an enrolled attestor can pre-sign any card and repoint its art

**No change in this build.** Read and accepted as a description of the trust
model rather than a defect: it is bounded by the signer's own allowance,
requires issuer-written roll membership, and moves no funds. Whether the roll
should be narrower than "any member may sign any card" is a product decision and
is not settled here.

## L4 — same-ledger duplicate signature over-spends the signer's own allowance

**No change in this build.** Confirmed as described, and it is self-harm only:
the slot's own allowance drops by two for one signature, consistent with the
documented spend-before-emit stance at `manager.c:730-741`. A guard would need
per-card state, which costs issuer reserve on every signed card to prevent a
signer from wasting their own allowance. Left open rather than closed — if it
is worth a guard, the cheap version is a check the signer can make off-ledger
before submitting a second Invoke.

---

## Out of scope items you flagged

1. **Deployment bindings.** Agreed, and `hook/live-test.mjs` phase `e` already
   asserts the installed side: which hooks are on which account, what they fire
   on, what they may emit, and that the two accounts point at each other.
2. **The installed wasm bytes.** Also agreed. `./hook/build.sh` prints the four
   HookHashes and writes `build/SOURCES.sha256`, so the match is checkable
   today; proving it against a deployment is the proof tier's job.
3. **Loader input validation.** Enforced at the admin door as well as at the
   read: `manager.c`'s `admin()` refuses any table record that is not one of the
   five shapes `namespaces.h` declares. That was the previous audit's finding 2
   and its live-test coverage is phase `g`.

## Verified on a rebuilt deployment — our testing, not the review's

Everything in this section is work we did after the review was delivered. None of
it was performed, checked, or endorsed by the reviewer, and none of it widens what
the review covered. It is recorded here because it is the evidence behind the
statuses in the table above.

Q1 required a fresh issuer, so the whole thing was redeployed on new accounts and
re-verified from empty rather than patched in place. Testnet, 2026-09-08:

- shop `rQBBk2D3ekgTM2unwLxgZYMHVX65yYepai`, issuer
  `raYnSBGqJ3eLDxPoJzBBNPAHrd7mUJxXpW`, claim authority
  `rNBqhEhM7T8ZTRAts9ExACQu3T4KzKroj`
- **`hook/live-test.mjs`: 435/435 passed**, every phase `a` through `o`, with the
  edition counter assertions rewritten for per-subject keying.
- **`hook/remarks-check.mjs --verify`: 95 cards, 0 un-annotated, 0 partial, 0
  mismatched, 0 `tecNO_TARGET`.** The rebuild matches every card the hook wrote,
  on a deployment where nothing was minted under an older DOMAIN — hence 0
  rehosted, against 218 on the previous issuer.
- **`hook/deliveries.mjs`: 19 triggers, 19 packs delivered whole, 0 short.**
- **The four installed `HookHash` values on ledger match the four `build.sh`
  prints** — mint `29E5097B…`, manager `E8655E53…`, shop `C650B566…`, doors
  `C16AC4E5…`. That is the "installed bytecode" item, checked by us on this
  testnet deployment. It does **not** convert the review into one that covered
  the installed bytes: the review read source at a pinned commit and that is what
  it says. Anyone can repeat this check with one `account_objects` call against
  the two accounts.

The per-subject keying is visible in the state itself: **95 cards minted across
68 counters**, so 27 of them landed on a subject already minted and incremented a
counter rather than creating one. `MINTCOUNT` reads 95 and the counters sum to 95.
The keys are nine bytes — `XC-01-062  4` — and the reserve is 13.6 XAH where
per-code keying would have locked roughly one entry per card.

Phase `m` is also worth naming here, because it settles something the report
listed under "assumptions accepted, not re-verified". The emission-forgery bar
was a read of the xahaud transactor rather than a test. It is now a test: an
attacker hook on this network tried to emit a Payment speaking as the shop, and
`forge: xahaud refused to emit a payment that speaks as the shop` with
`nothing was emitted toward the issuer (0)` is the live result.

## What changed

| file | change |
| --- | --- |
| `hook/src/mint.c` | Q1 — the edition counter is keyed on `SUBJECT_LEN`, the nine bytes covering theme and subject, rather than the full card code. |
| `hook/src/trigger.h` | L1 — the callback comment now describes an L+2 Remit failure correctly. Written to the same line count, so no HookHash moved for it. |
| `hook/state.mjs` | `--intent` no longer claims delivery it cannot know about, and points at the reconciler. Counters are matched positively by key shape, so a stray — or a leftover per-code counter — is named rather than counted as a subject. |
| `hook/lib/namespace.mjs` | `CODE_LEN` replaced by `SUBJECT_KEY_LEN`. |
| `hook/live-test.mjs` | the counter assertions allow a pack to move fewer counters than it mints cards, and check the run of editions per subject. |
| `hook/deliveries.mjs` | new — reconciles purchases against the cards actually on the ledger. |
| `hook/remarks-check.mjs` | new — finds un-annotated cards, and writes the missing remarks. |
| `resources/js/xahau-client.js`, `status.js`, `codex.js` | the counter key shortens; the per-subject sum becomes a direct read; status derives a counter's rarity from the set's block shape, since the key no longer carries it. |
| `README.md`, `DEPLOYING.md`, `DEPLOYING-PRODUCTION.md` | rewritten for per-subject editions, and the stale reserve figures corrected. |
| `resources/views/whitepaper.blade.php` | unchanged, which is the point — §7 and the §2 caption are now true as written. |
