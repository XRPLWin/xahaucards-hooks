#include "trigger.h"

#define GRANT_RECIPIENT_AT 0U
#define GRANT_THEME_AT 20U
#define GRANT_LEN 22U

#define GRANT_CEILING_DROPS 10000000ULL

static int64_t
grant(const uint8_t *payload, int64_t len)
{

#line 133
    if (len != (int64_t)GRANT_LEN)
        NOPE("xahaucards: a GRANT parameter of exactly 22 bytes is required — a 20 byte account id, then two theme digits.");

    uint8_t shop_acc[20];
    hook_account(SBUF(shop_acc));

    uint8_t issuer_acc[20];

    if (hook_param(SBUF(issuer_acc), "ISSUER", 6) != 20)
        NOPE("xahaucards: this hook was installed without an ISSUER parameter.");

    uint8_t claim_acc[20];

#line 153
    if (state_foreign(SBUF(claim_acc), SBUF(KEY_CLAIM), SBUF(NS_SETTINGS), SBUF(issuer_acc)) != 20)
        NOPE("xahaucards: no claim authority is configured on the issuer.");

    uint8_t sender[20];
    otxn_field(SBUF(sender), sfAccount);

    if (!BUFFER_EQUAL_20(claim_acc, sender))
        NOPE("xahaucards: only the claim authority may grant a pack.");

    int64_t left = state(0, 0, SBUF(KEY_GRANTS));

#line 170
    if (left <= 0)
        NOPE("xahaucards: the grant allowance is used up — load more before claiming again.");

    uint8_t dkey[TKEY_THEME_LEN];
    dkey[0] = 'D';
    dkey[1] = payload[GRANT_THEME_AT];
    dkey[2] = payload[GRANT_THEME_AT + 1U];

    uint8_t shape[64];

#line 185
    if (state_foreign(SBUF(shape), SBUF(dkey), SBUF(NS_TABLE), SBUF(issuer_acc)) != SHAPE_LEN)
        NOPE("xahaucards: no such set — check the theme digits in the GRANT parameter.");

    dkey[0] = SALE_KEY_KIND;

    uint8_t sale[1];

#line 204
    if (state_foreign(SBUF(sale), SBUF(dkey), SBUF(NS_TABLE), SBUF(issuer_acc)) != 1
        || sale[0] != SALE_OPEN)
        NOPE("xahaucards: that set is not on sale — free packs stop with it.");

    uint8_t acc_keylet[34];

#line 228
    if (util_keylet(SBUF(acc_keylet), KEYLET_ACCOUNT, payload + GRANT_RECIPIENT_AT, 20, 0, 0, 0, 0) != 34)
        NOPE("xahaucards: could not derive the recipient's account keylet.");

    int64_t exists = slot_set(SBUF(acc_keylet), 1) == 1;

    if (exists)
    {
        int64_t acc_flags = slot_subfield(1, sfFlags, 2) == 2 ? slot(0, 0, 2) : 0;

#line 244
        if (acc_flags > 0 && (acc_flags & (lsfDisallowIncomingRemit | lsfRequireDestTag | lsfDepositAuth)))
            NOPE("xahaucards: that account is blocking incoming Remits, requires a destination tag or has deposit authorisation on — it cannot be granted a pack.");
    }

    int64_t budget = state_foreign(0, 0, SBUF(KEY_BUDGET), SBUF(NS_SETTINGS), SBUF(issuer_acc));

#line 252
    if (budget <= 0)
        NOPE("xahaucards: the mint budget is not configured on the issuer.");

    int64_t newacct = state_foreign(0, 0, SBUF(KEY_NEWACCT), SBUF(NS_SETTINGS), SBUF(issuer_acc));

#line 267
    if (newacct <= 0)
        NOPE("xahaucards: the new account allowance is not configured on the issuer.");

    uint64_t forward = (uint64_t)budget + (exists ? 0ULL : (uint64_t)newacct);

#line 285
    if (forward > GRANT_CEILING_DROPS)
        NOPE("xahaucards: a grant would forward more than 10 XAH — check the budget and new account settings.");

#line 290
    if (etxn_reserve(1) != 1)
        NOPE("xahaucards: could not reserve the trigger emission.");

    uint8_t txid[32];

#line 298
    if (otxn_id(SBUF(txid), 0) != 32)
        NOPE("xahaucards: could not read the transaction id.");

    uint8_t counter[GRANTS_LEN];
    UINT32_TO_BUF(counter, (uint32_t)left - 1U);

#line 308
    if (state_set(SBUF(counter), SBUF(KEY_GRANTS)) < 0)
        NOPE("xahaucards: could not spend the grant allowance.");

    uint8_t intent[INTENT_LEN];

    BLIT20(intent + INTENT_BUYER_AT, payload + GRANT_RECIPIENT_AT);
    intent[INTENT_THEME_AT] = payload[GRANT_THEME_AT];
    intent[INTENT_THEME_AT + 1U] = payload[GRANT_THEME_AT + 1U];
    intent[INTENT_STATUS_AT] = INTENT_GRANTED;

    if (state_set(SBUF(intent), SBUF(txid)) < 0)
        NOPE("xahaucards: could not record the grant.");

    fire_trigger(shop_acc, issuer_acc, forward);

    DONE("xahaucards: granted — the cards are being minted.");
}

static int64_t
allow(const uint8_t *value, int64_t len)
{

#line 349
    if (len != (int64_t)GRANTS_LEN)
        NOPE("xahaucards: an ALLOW parameter of exactly four big-endian bytes is required.");

    uint8_t shop_acc[20];
    hook_account(SBUF(shop_acc));

    uint8_t sender[20];
    otxn_field(SBUF(sender), sfAccount);

    if (!BUFFER_EQUAL_20(shop_acc, sender))
        NOPE("xahaucards: only the shop itself may set the grant allowance.");

    uint32_t allowance = UINT32_FROM_BUF(value);

    if (allowance > GRANTS_MAX)
        NOPE("xahaucards: that allowance is larger than this door will set — check the digits.");

    uint8_t counter[GRANTS_LEN];
    UINT32_TO_BUF(counter, allowance);

#line 374
    if (state_set(SBUF(counter), SBUF(KEY_GRANTS)) < 0)
        NOPE("xahaucards: could not set the grant allowance.");

    DONE("xahaucards: grant allowance set.");
}

int64_t
hook(uint32_t reserved)
{
    _g(1, 1);

    int64_t tt = otxn_type();

#line 394
    if (tt != ttINVOKE)
        DONE("xahaucards: not an Invoke, passing.");

    uint8_t door[64];
    int64_t len;

    if ((len = otxn_param(SBUF(door), "GRANT", 5)) > 0)
        return grant(door, len);

    if ((len = otxn_param(SBUF(door), "ALLOW", 5)) > 0)
        return allow(door, len);

#line 427
    NOPE("xahaucards: an Invoke here must carry GRANT or ALLOW.");
}
