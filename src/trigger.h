#ifndef XAHAUCARDS_TRIGGER_H
#define XAHAUCARDS_TRIGGER_H

#include "hookapi.h"
#include "namespaces.h"

#define EMIT_DETAILS_LEN 138U

#define BLIT20(dst, src) \
{ \
    uint64_t *d_ = (uint64_t *)(dst); \
    const uint64_t *s_ = (const uint64_t *)(src); \
    d_[0] = s_[0]; d_[1] = s_[1]; \
    *(uint32_t *)((uint8_t *)(dst) + 16) = *(const uint32_t *)((const uint8_t *)(src) + 16); \
}

static uint8_t ptxn[260] = {

  0x12U, 0x00U, 0x00U,
  0x22U, 0x80U, 0x00U, 0x00U, 0x00U,
  0x24U, 0x00U, 0x00U, 0x00U, 0x00U,
  0x20U, 0x1AU, 0x00U, 0x00U, 0x00U, 0x00U,
  0x20U, 0x1BU, 0x00U, 0x00U, 0x00U, 0x00U,
  0x61U, 0x40U, 0x00U, 0x00U, 0x00U, 0x00U,
               0x00U, 0x00U, 0x00U,
  0x68U, 0x40U, 0x00U, 0x00U, 0x00U, 0x00U,
               0x00U, 0x00U, 0x00U,
  0x73U, 0x21U, 0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,
  0x81U, 0x14U, 0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,
  0x83U, 0x14U, 0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,

};

#define P_LEN 260U

#define P_EMIT_AT 122U

#define P_FLS_OUT (ptxn + 15U)
#define P_LLS_OUT (ptxn + 21U)
#define P_AMOUNT_OUT (ptxn + 26U)
#define P_FEE_OUT (ptxn + 35U)
#define P_ACCOUNT_OUT (ptxn + 80U)
#define P_DEST_OUT (ptxn + 102U)
#define P_EMIT_OUT (ptxn + P_EMIT_AT)

_Static_assert(
    P_EMIT_AT + EMIT_DETAILS_LEN == P_LEN,
    "the trigger template does not end where its EmitDetails does."
);

_Static_assert(
    sizeof(ptxn) == P_LEN,
    "the trigger template is not the length its offsets assume."
);

static inline __attribute__((always_inline)) int64_t
fire_trigger(const uint8_t *shop_acc, const uint8_t *issuer_acc, uint64_t drops)
{
    BLIT20(P_ACCOUNT_OUT, shop_acc);
    BLIT20(P_DEST_OUT, issuer_acc);

    UINT64_TO_BUF(P_AMOUNT_OUT, drops | 0x4000000000000000ULL);

    uint32_t fls = (uint32_t)ledger_seq() + 1;
    uint32_t lls = fls + 4;

    *((uint32_t *)(P_FLS_OUT)) = FLIP_ENDIAN_32(fls);
    *((uint32_t *)(P_LLS_OUT)) = FLIP_ENDIAN_32(lls);

#line 203
    if (etxn_details(P_EMIT_OUT, EMIT_DETAILS_LEN) != EMIT_DETAILS_LEN)
        NOPE("xahaucards: unexpected emit details length.");

    int64_t fee = etxn_fee_base(ptxn, P_LEN);

#line 219
    if (fee < 0)
        NOPE("xahaucards: could not price the trigger.");

    uint8_t *f = P_FEE_OUT;
    *f++ = 0b01000000 + ((fee >> 56) & 0b00111111);
    *f++ = (fee >> 48) & 0xFFU;
    *f++ = (fee >> 40) & 0xFFU;
    *f++ = (fee >> 32) & 0xFFU;
    *f++ = (fee >> 24) & 0xFFU;
    *f++ = (fee >> 16) & 0xFFU;
    *f++ = (fee >> 8) & 0xFFU;
    *f++ = (fee >> 0) & 0xFFU;

    uint8_t emit_hash[32];

    if (emit(SBUF(emit_hash), ptxn, P_LEN) < 0)
        NOPE("xahaucards: could not emit the trigger.");

    return 0;
}

int64_t
cbak(uint32_t what)
{
    _g(1, 1);

#line 293
    if (otxn_slot(1) != 1)
        DONE("xahaucards: callback could not slot the trigger.");

    if (slot_subfield(1, sfEmitDetails, 2) != 2)
        DONE("xahaucards: callback found no emit details.");

    if (slot_subfield(2, sfEmitParentTxnID, 3) != 3)
        DONE("xahaucards: callback found no parent transaction id.");

    uint8_t txid[32];

#line 306
    if (slot(SBUF(txid), 3) != 32)
        DONE("xahaucards: callback could not read the parent transaction id.");

    if (what == 0)
    {

        uint8_t engine = 0;

        int64_t read = meta_slot(4) == 4
            && slot_subfield(4, sfTransactionResult, 5) == 5
            && slot(SVAR(engine), 5) == 1;

        if (read && engine == 0)
        {
            state_set(0, 0, SBUF(txid));

#line 365
            DONE("xahaucards: pack delivered.");
        }
    }

    uint8_t intent[INTENT_LEN];

    if (state(SBUF(intent), SBUF(txid)) == (int64_t)INTENT_LEN)
    {

        intent[INTENT_STATUS_AT] = intent[INTENT_STATUS_AT] == INTENT_GRANTED
            ? INTENT_LAPSED
            : INTENT_FAILED;

        state_set(SBUF(intent), SBUF(txid));
    }

#line 402
    DONE("xahaucards: the trigger did not deliver — the pack is recorded as owed.");
}

#endif
