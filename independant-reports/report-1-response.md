# Response to report-1

Response to [report-1/hook-audit.md](report-1/hook-audit.md), the live testnet
audit of these hooks. Written 2026-09-06 against the build whose digests are in
the README beside this directory. The report's own files are unchanged.

Thank you for the work. Every finding was reproduced from the report's
description alone, and the three code fixes it ranked highest are in this
build. The fourth is in too. The fifth is unchanged and says why.

## Finding 1 — attestors could sign any URIToken from this issuer

**Fixed** in `src/manager.c`, `attest()`.

The hook checked the token's issuer and the URI's length and nothing about its
content, so a token this account minted by hand with an `http://` URI took a
`CardSignature` and an image remark sliced from offset 13 of the junk.

`attest()` now refuses a token whose URI is not one `mint.c` would have
written: the `xahaucards://` scheme, the `XC-` marker, a dash at every place
the card code has one, and room for at least one edition digit past the code,
which is `URI_MIN_LEN` in `src/namespaces.h`. Every byte compared is a byte
`mint.c` writes as a literal, so the two hooks cannot disagree about the format
without disagreeing about the format itself. The refusal is
`that URIToken is not a XahauCards card`, and it comes before the roll is
consulted for a slot.

Not checked: the digits between the dashes. Nothing in the signing path reads
them, and a token with dashes in the right places and junk between them still
had to be minted by this account on purpose, which makes it the issuer's own
lie rather than an attestor's.

## Finding 2 — card-table lengths were not bounded

**Fixed** on both sides of the ledger.

The length byte in front of each table field is not a description of the
record, it is an instruction that moves `mint.c`'s write cursor. A subject row
whose byte said 200 on a 25 byte record walked that cursor through the faction
key and 175 bytes of stack into the card's JSON, and four such fields stacked
could carry the cursor off the end of the metadata buffer altogether.

- `src/manager.c`, `admin()` — the admin door refuses to write a table record
  that is not exactly one of the five shapes `src/namespaces.h` declares:
  `S` rows of 25 bytes with the name length inside 1..20; `T`, `F` and `R`
  labels of 13 bytes with the name length inside 1..12; `D` shapes of 20 bytes
  where every block has at least one subject and none past 999; `O` sale
  records of exactly the open byte. Theme and subject digits in the key have
  to be digits, so a set the shop could sell can never carry a binary theme
  into a card code. A key of any other kind under the table selector is
  refused. A delete (a single zero byte) is exempt, so a retired row stays
  removable. Only the table is shape-checked: settings are read into fixed
  buffers and refused at the wrong width by the hook that needs them, and a
  roll entry is checked when it is used.
- `src/mint.c` — the same bounds are enforced where the bytes are read, as the
  backstop for a table written before the door checked. A row or label that
  fails them rolls the pack back, which reaches the shop's callback and leaves
  a recorded claim, rather than minting garbage sealed immutable.

The `cards:export` loader in the working repository already enforced these
widths from the outside. What changed is that they are now a property of the
ledger rather than of the tool that happened to write it.

## Finding 3 — no refund when the mint does not deliver

**Not changed. Documented.** This is a design position rather than an
oversight, and the README now states it plainly under "What the shop is, and
is not": the shop is an operated hot wallet, not an escrow.

The reasoning, briefly. A callback runs after the fact and cannot roll anything
back. A refund from it would be a second emitted Payment whose own failure
nothing reports, whose amount and destination the hook would have to derive
under exactly the conditions that made the mint fail, and which would turn a
failed trigger into a free retry if any failure were ever buyer-inducible. The
hooks instead write the purchase down before emitting and mark it failed in
the callback, so every failed purchase stays visible on the shop's account and
is refunded by a Payment signed with the shop's key. See the callback in
`src/trigger.h` and the intent record in `src/namespaces.h`.

The report is right that this makes the shop a custodian between the Payment
and the cards. It is, and it says so now.

## Finding 4 — 1 XAH top-up indistinguishable from a mistaken purchase

**Fixed** in `src/shop.c`.

A top-up is no longer an amount. A Payment carrying a `TOPUP` hook parameter,
with any value, passes through as a top-up whatever it carries; a Payment
without one is a purchase attempt and is held to the exact price. 1 XAH with no
parameter is now refused like any other wrong amount. The "price collides with
the top-up" refusal went with the amount, since there is nothing left to
collide with. Nothing in the site's buy flow adds the parameter, so a buyer
cannot reach the top-up path by mistake.

## Finding 5 — pack RNG is sha512half(parent txid || ledger_last_hash)

**Unchanged.** As the report says, this is the normal construction for the
position the hook is in, and it is documented at length at the top of
`src/mint.c` and `src/shop.c`: the seed is read one ledger after the buyer
committed, from a hash that did not exist when they signed. The residual the
report names, that whoever includes the trigger picks among the hashes of at
most five ledgers, is a validator's position, not a buyer's, and is stated in
`src/trigger.h` where the emission window is chosen.

## Observations in the report

- `THEME` not digit-checked: closed at the admin door rather than on the buy
  path. A `D` record can no longer exist under a non-digit theme, so a binary
  `THEME` can only ever miss the table, which is what the report saw.
- `NS=E` write of `MINTCOUNT` failing with `could not write the entry`: not
  reproduced here and not changed. The most likely cause is the issuer's
  reserve, since a new state entry costs one. It is an operator path the loader
  never uses.
- `followup-results.json` records one `HIGH` finding, "Exact pack price buy
  failed after restoring 8-byte PRICE", against a transaction whose engine
  result in the same file is `tesSUCCESS` with no hook executions captured. The
  purchase went through; the harness appears not to have read the executions.
  Not treated as a finding.

## Cost of the changes

Worst-case guard counts, before and after, as `build.sh` reports them:

| hook | before | after |
| --- | --- | --- |
| shop | 149 / 650 | 149 / 644 |
| doors | 149 / 812 | 149 / 812 |
| mint | 28997 | 29555 |
| manager | 22741 | 30714 |

The shop got marginally cheaper for buyers. The mint's increase lands on the
shop's trigger fee, well under a thousand drops a pack. The manager's increase
is the table validation, multiplied by the seven records an admin batch can
carry, and is paid by the issuer on a table load and by an attestor on a
signature. All four remain well inside the 65535 ceiling.

## Verification

The live test suite in the working repository carries a case for each fix:
the two top-up rules and the 1 XAH refusal in phase a, the non-card token
refusal in phase d, and thirteen malformed table records refused at the door
in phase g, with a well formed row still landing and being deleted again.
