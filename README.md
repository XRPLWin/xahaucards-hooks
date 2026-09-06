# XahauCards — the hooks

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
| `shop.wasm` | 428610602ba0666cf90ce181537586e65b3990d314483c9aff49af7fbbd51c10 |
| `doors.wasm` | 40d04eafa7c8aa3d8ae54dd7c4006c8a5008d8413e9f5a3336ea424592343503 |
| `mint.wasm` | 9fbbca1286faf17d591f6616a291fe91d27bd0009d3ccb58652b754791f46d30 |
| `manager.wasm` | 1c73661f563cb8d5a8f16b4bb865fca980d26d471ced0ce5df0f6d097bdbdfb3 |

`build/SOURCES.sha256` carries the same digests alongside the sources they were
built from.

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

