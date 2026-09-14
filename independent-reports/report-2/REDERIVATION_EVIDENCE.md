# Independent pack re-derivation — evidence (2026-09-08)
Free cross-check promised in the DM: reproduce a real minted pack from PUBLIC DATA ONLY, no repo, no operator input.

Formula (from mint.c): seed = sha512half(buyer_payment_txid ‖ ledger_last_hash); card[i] = sha512half(seed ‖ i);
7 bytes used → roll[0]=rarity (thresholds 150/220/246/254), roll[1..2]=subject in the D-record block, roll[3..4]=attack, roll[5..6]=health.

Inputs (public, on Xahau testnet):
- buyer payment P = 712EEBCF361F07C309429E17D4078AC3798D94D86F9DA87AFA1D8C5270EF4D1B
- ledger_last_hash (hash of the ledger P landed in) = EAF06F0FD233530AFDD1B34C5980B085B16F67E8794F7A3EE3148C32F6D4B883
- theme 01 shape (D-record, on-chain HookState): C[0,66] U[200,42] R[350,24] E[450,12] L[500,6]

On-chain minted cards (5):  XC-01-026-C-04-04, XC-01-009-C-03-01, XC-01-205-U-08-06, XC-01-222-U-08-08, XC-01-221-U-06-05
Re-derived from public data: same five (identical as a set; mint/creation order differs, not a fairness property).

RESULT: MATCH = true. The pack is a pure deterministic function of the payment txid and a ledger hash that
post-dates the buyer's signature. No operator randomness, no lever. Reproducible by anyone: `node rederive.mjs`.
