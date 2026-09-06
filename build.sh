#!/usr/bin/env bash
#
# Compiles all three hooks to WASM.
#
#   ./hook/build.sh
#
# Output lands in hook/build/, which is what hook/deploy.mjs installs:
#
#   shop.wasm      goes on the shop account
#   mint.wasm      goes on the issuer
#   manager.wasm   goes on the issuer, beside mint
#
# Everything happens inside a container, so the only requirement on the host is
# docker.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

IMAGE="xahaucards-hook-toolchain"
GUARD_IMAGE="xahaucards-hook-guard"

# Which hooks exist, and which of them has a callback.
#
# The two on the shop do, and neither may stop. That is not a style choice — a
# hook that exports cbak has a 22 byte sfEmitCallback appended to the
# sfEmitDetails of everything it emits, so the export list decides the width of
# a field the C source hard-codes as an offset. The two have to agree, and this
# is where they are told to.
#
# Both include trigger.h, which hard-codes the wide form at 138 and says so at
# the top. Dropping either from this list emits a blob 22 bytes short that the
# network cannot parse.
HOOKS=(shop doors mint manager)
CBAK_HOOKS=(shop doors)

mkdir -p "$HERE/build"

# Two images from one Dockerfile, because the compiler and the guard checker
# need different Debian releases. See the note at the top of the Dockerfile.
# Always pass --target: a bare `docker build hook/` would give you the last
# stage, which is the guard image, not the toolchain.
for spec in "toolchain:$IMAGE" "guard:$GUARD_IMAGE"; do
    target="${spec%%:*}"
    tag="${spec#*:}"
    if ! docker image inspect "$tag" >/dev/null 2>&1; then
        echo "  building   $tag (first run only)"
        docker build -q --target "$target" -t "$tag" "$HERE" >/dev/null
    fi
done

echo "  compiling  ${HOOKS[*]}"

# --allow-undefined because every hook API call is an import supplied by
# xahaud. --no-entry because a hook has no main.
#
# --export=cbak is passed for the shop and nothing else. Exporting it where it
# is not wanted is not inert: xahaud then expects a 138 byte sfEmitDetails
# instead of 116, and every emission from that hook comes out unparseable.
docker run --rm -v "$HERE:/hook" -w /hook "$IMAGE" bash -c '
set -euo pipefail
for hook in '"${HOOKS[*]}"'; do
    exports="-Wl,--export=hook"
    case " '"${CBAK_HOOKS[*]}"' " in
        *" $hook "*) exports="$exports -Wl,--export=cbak" ;;
    esac

    clang \
        --target=wasm32-unknown-unknown \
        -O2 -nostdlib -fno-builtin \
        -I include -I src \
        -Wl,--allow-undefined \
        -Wl,--no-entry \
        $exports \
        -o "build/raw-${hook}.wasm" \
        "src/${hook}.c"
done
'

# hook-cleaner is not optional, and not only about exports.
#
# Trimming the export list by hand is not enough: a module with clang's helper
# functions still left in it is rejected by HookSet with a bare temMALFORMED,
# verified by A/B — identical source, cleaned module tesSUCCESS, hand-stripped
# module temMALFORMED. It also renumbers the function index space to match the
# dead code it removes, and gets that wrong once a module carries several
# internal functions, so keep the function count low and let the check below
# catch it when that breaks.
docker run --rm -v "$HERE:/hook" -w /hook "$IMAGE" bash -c '
set -euo pipefail
for hook in '"${HOOKS[*]}"'; do
    hook-cleaner "build/raw-${hook}.wasm" "build/${hook}.wasm" >/dev/null
    rm -f "build/raw-${hook}.wasm"
done
'

# guard-checker runs the loop-guard validation xahaud applies at SetHook, so an
# unbounded loop is caught here instead of a temMALFORMED at deploy time. It
# exits non-zero when validation fails, which set -e turns into a failed build.
#
# The worst-case execution count it reports is worth seeing: that number is what
# the fee calculation is based on, so a jump in it after a source change is the
# early warning that a hook got more expensive to run.
#
# Note whose bill each one lands on, because it is not the same account for all
# three. The shop's count is charged to the buyer, on the Payment they sign.
# The issuer's two are charged to the shop, on the trigger it emits — a strong
# transactional stakeholder never pays for its own hook chain. So mint.c getting
# more expensive does not show up on the issuer's balance; it shows up in the
# fee the shop pays out of the pack price.
docker run --rm -v "$HERE:/hook" -w /hook "$GUARD_IMAGE" bash -c '
set -euo pipefail
for hook in '"${HOOKS[*]}"'; do
    count="$(guard-checker "build/${hook}.wasm" | sed -n "s/.*execution count: //p" | tr "\n" "/" | sed "s#/\$##")"
    echo "  guarding   ${hook} (worst-case execution count: ${count:-unknown})"
done
'

CBAK_LIST="${CBAK_HOOKS[*]}" HOOK_LIST="${HOOKS[*]}" node --input-type=module -e "
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';

const hooks = process.env.HOOK_LIST.split(' ');
const withCbak = new Set(process.env.CBAK_LIST.split(' '));

for (const name of hooks) {
  const wasm = readFileSync('$HERE/build/' + name + '.wasm');
  let mod;
  try { mod = new WebAssembly.Module(wasm); }
  catch (e) { console.error('  ' + name + ': invalid module — ' + e.message); process.exit(1); }

  const exports = WebAssembly.Module.exports(mod).map((x) => x.name).sort();

  if (exports.some((n) => !['cbak', 'hook'].includes(n))) {
    console.error('  ' + name + ' exports ' + exports.join(', ') + ' — HookSet allows only hook and cbak');
    process.exit(1);
  }

  // The check that matters, in both directions. A missing cbak on the shop
  // means an emitted trigger nothing ever reports the failure of, and a buyer
  // who paid for a pack that silently never arrived. An unexpected cbak on the
  // issuer's hooks widens their sfEmitDetails by 22 bytes past what the C
  // source lays out, and every card they emit comes out unparseable.
  const hasCbak = exports.includes('cbak');
  const wantsCbak = withCbak.has(name);

  if (hasCbak !== wantsCbak) {
    console.error('  ' + name + (wantsCbak
      ? ' is missing cbak — its emissions would go unreported'
      : ' exports cbak — its EmitDetails would be 138 bytes, not the 116 its templates assume'));
    process.exit(1);
  }

  console.log('  built      build/' + name + '.wasm (' + wasm.length + ' bytes'
    + (hasCbak ? ', with callback' : '') + ')');

  // What the ledger will call it. A HookDefinition is keyed by the SHA-512-half
  // of its wasm, not the SHA-256 that sha256sum and SOURCES.sha256 give — so
  // this is the value to compare against a Hook object's HookHash, and the one
  // the public README's table links to on the explorer.
  console.log('             HookHash ' + createHash('sha512').update(wasm).digest('hex').slice(0, 64).toUpperCase());
}
" || exit 1

# The compiled hooks are committed, so build/ can drift from src/ in a way a
# fresh-compile-only workflow made impossible. This stamp is what makes the
# drift visible: rebuild and diff it, and a stale artifact shows up as a
# changed source hash with an unchanged wasm.
HOOK_LIST="${HOOKS[*]}" node --input-type=module -e "
import { readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';

const sha = (f) => createHash('sha256').update(readFileSync('$HERE/' + f)).digest('hex');
const hooks = process.env.HOOK_LIST.split(' ');

const files = [
  ...hooks.map((h) => 'src/' + h + '.c'),
  // Both shared headers, or the stamp proves less than it claims: trigger.h
  // carries the transaction template, the emitter and the callback for two of
  // these wasms, so a change to it changes what was built with none of the .c
  // files moving.
  'src/namespaces.h',
  'src/trigger.h',
  ...hooks.map((h) => 'build/' + h + '.wasm'),
];

writeFileSync('$HERE/build/SOURCES.sha256',
  files.map((f) => sha(f) + '  ' + f).join('\n') + '\n');
" || exit 1

# The published copy, refreshed on every build so it cannot go stale.
#
# Skipped when this IS the published copy — the exporter lives in the working
# repository and is not shipped, so a reader who runs ./build.sh here should get
# the four wasm files and nothing else.
#
# Refreshing it here rather than by hand is the whole guarantee: the public
# sources and the public wasm are written in the same breath as the ones that
# get deployed, so the repository people inspect cannot describe a build that
# was never made. See the note at the top of export-public.mjs.
#
# The same test decides what to print at the end. This script is shipped
# verbatim, so in the public copy it would otherwise finish by telling a reader
# to run a deploy script they do not have — and what they actually want to do
# next is compare a digest.
if [ -f "$HERE/export-public.mjs" ]; then
    node "$HERE/export-public.mjs" >/dev/null || exit 1
    echo "  exported   hook-public-repo/ (sources without comments, same bytecode)"

    echo
    echo "  Deploy them with:"
    echo "    XAHAU_HOOK_SEED_SHOP=s... XAHAU_HOOK_SEED_ISSUER=s... node hook/deploy.mjs"
else
    echo
    echo "  Compare the HookHash printed above against the Hook objects on the two"
    echo "  accounts. A match means the source here is the code on ledger."
fi
