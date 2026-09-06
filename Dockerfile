# Toolchain for building the card hook.
#
# Two stages, built as two separate images by build.sh via --target:
#
#   toolchain  clang + hook-cleaner, on bookworm
#   guard      guard-checker, on trixie
#
# They are split because they cannot share a base. guard-checker's release
# binary needs GLIBCXX_3.4.32, which bookworm's libstdc++ (3.4.30) does not
# provide, and trixie ships clang 19 against bookworm's clang 14 — moving the
# compiler would change the codegen of a wasm that is committed and on ledger.
# So the compiler stays pinned on bookworm and only the checker moves.
#
# Self contained on purpose. The hook toolchain images that older guides point
# at (xrpllabsofficial/xrpld-hooks-toolchain, xahau/hooks-toolchain and
# friends) are all gone from the registries.

FROM debian:bookworm-slim AS toolchain

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      clang lld make gcc libc-dev git ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# Pinned to a commit, not a branch: what goes on ledger should not change
# because an upstream default branch moved.
ARG HOOK_CLEANER_REF=b856a3614c00361f108d07379f5892e7347bb994
RUN git clone https://github.com/XRPLF/hook-cleaner-c /tmp/hook-cleaner \
 && cd /tmp/hook-cleaner \
 && git checkout "${HOOK_CLEANER_REF}" \
 && make \
 && install -m 0755 hook-cleaner /usr/local/bin/hook-cleaner \
 && rm -rf /tmp/hook-cleaner

WORKDIR /hook


# guard-checker runs the loop-guard validation xahaud's SetHook applies, so an
# unbounded loop is caught here instead of at deploy time.
#
# tequdev/guard-checker, not RichardAH/guard-checker. The latter is a 2023
# hand-copied snapshot of Guard.h that predates the GuardRuleFix20250131 and
# GuardRuleDepth32 rule updates, so it validates against rules xahaud no longer
# enforces and can pass a module the ledger rejects. tequdev's builds from
# Xahau/xahaud itself as a submodule and tracks its releases.
#
# Prebuilt release binary rather than a source build: pinning a tag plus its
# sha256 is a stronger guarantee than pinning a ref that still has to compile
# the same way twice, and it keeps the xahaud source tree out of the image. Do
# not use the upstream install.sh — it fetches from Xahau/guard-checker, which
# does not exist.
#
# The musl assets are not an alternative to the trixie base: they are linked
# against musl's loader, not statically linked, so they do not run on Debian.
FROM debian:trixie-slim AS guard

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl libstdc++6 \
 && rm -rf /var/lib/apt/lists/*

ARG GUARD_CHECKER_VERSION=v0.1.1
RUN set -eux; \
    case "$(dpkg --print-architecture)" in \
      amd64) asset=guard-checker-linux-x64-gnu; \
             sha=5320dec78b6138fa103c313957584af3d0a9ae7a028f47077852ad8b747f271d ;; \
      arm64) asset=guard-checker-linux-arm64-gnu; \
             sha=59a1100bdd84331e3612ca80f6e624a7fc748c71a57c041915899b5564d7e1b7 ;; \
      *) echo "no guard-checker build for $(dpkg --print-architecture)" >&2; exit 1 ;; \
    esac; \
    curl -fsSL -o /tmp/guard-checker \
      "https://github.com/tequdev/guard-checker/releases/download/${GUARD_CHECKER_VERSION}/${asset}"; \
    echo "${sha}  /tmp/guard-checker" | sha256sum -c -; \
    install -m 0755 /tmp/guard-checker /usr/local/bin/guard-checker; \
    rm -f /tmp/guard-checker

WORKDIR /hook
