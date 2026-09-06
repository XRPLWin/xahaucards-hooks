# XahauCards Hooks

The four hooks that sell and mint XahauCards, exactly as deployed, with the
container and the script that build them.

This is a **published copy**. It is generated from the project's working
repository and is not where the code is edited, so it takes no pull requests —
issues and questions are welcome.

## What you can check

The bytecode of a deployed hook is committed to on ledger: each account's
`Hook` object names a `HookDefinition` by the sha256 of its wasm. So:

```bash
./build.sh
sha256sum build/*.wasm
```

If those digests match the `HookHash` entries on the two accounts, the code in
this repository is the code that is running. Nothing else has to be trusted —
not this README, not the person who published it.

The build runs entirely inside a container built from the `Dockerfile` here, so
the toolchain is pinned rather than whatever happens to be installed.

| file | sha256 |
| --- | --- |
| `shop.wasm` | 9d38306572c08dcf0e95ea969cffd2989e37169414028e88427820a6b5556917 |
| `doors.wasm` | 40d04eafa7c8aa3d8ae54dd7c4006c8a5008d8413e9f5a3336ea424592343503 |
| `mint.wasm` | 832ea89bdc10b152cdc525304496f199c4cb7d25c9ca4628e59caad4c3900ca5 |
| `manager.wasm` | d93905c5d31893c8277e9cbe6794a31aac174e9f2c8ef682146c0d5812cc4b9b |

`build/SOURCES.sha256` carries the same digests alongside the sources they were
built from.

Note that `HookHash` on ledger is the SHA-512-half of the wasm, while the table
above and `sha256sum` give SHA-256. Compare like with like: the `HookDefinition`
object a `Hook` entry points at is keyed by the SHA-512-half, so either recompute
that from `build/*.wasm` or trust the SHA-256 match against a build you made
yourself. The first independent report below tripped over this.

## The four hooks

| file | account | fires on | what it does |
| --- | --- | --- | --- |
| `src/shop.c` | shop | Payment | takes the money for a pack and triggers the issuer |
| `src/doors.c` | shop | Invoke | free packs, and the allowance that bounds them |
| `src/mint.c` | issuer | Payment | rolls five cards and mints them to the buyer |
| `src/manager.c` | issuer | Invoke | loads the card table, records dev signatures |

`src/namespaces.h` is the state layout all four share. `src/trigger.h` holds the
transaction the shop emits and the callback that follows it, and is compiled
into both of the shop's hooks.

`include/` is the Xahau Hook API, copied unmodified from upstream.

## What the shop is, and is not

The shop is an operated hot wallet, not an escrow. Read this before buying if
that distinction matters to you.

A purchase is one Payment to the shop, and it is final the moment the shop's
hook accepts it. The cards come from the issuer, one ledger later, on a
transaction the shop emits. If that transaction is refused or expires, nothing
on ledger reverses the Payment: the hooks do not refund automatically, by
design. What they do instead is write the purchase down before the shop emits —
buyer, set, status — and the shop's callback marks that record as failed when
the trigger does not deliver. Recovery is then a Payment signed with the shop's
key, made by the operator against that record, and the buyer's original
transaction id is the claim.

Why not refund from the hook: a callback runs after the fact and cannot roll
anything back, so a refund would be a second emitted Payment whose own failure
nothing reports, and whose amount and destination the hook would have to derive
under exactly the conditions that made the mint fail. The record-and-recover
path keeps every failed purchase visible on the shop's account rather than
trusting a second emission to land. The takings themselves are the shop's
balance and the operator can move them; the shop is not holding your money in
trust between the Payment and the cards, it is holding it.

The issuer is funded per pack by the shop and spends its own balance on the ten
transactions a pack takes. That balance is the operator's to top up.

## Independent reports

External reviews of these hooks live under `independant-reports/`, as
delivered, with a response beside each saying what changed because of it.

| report | response |
| --- | --- |
| [report-1](independant-reports/report-1/hook-audit.md) — live testnet audit, September 2026 | [report-1-response.md](independant-reports/report-1-response.md) |

