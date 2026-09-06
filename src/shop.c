#include "trigger.h"

#define TOPUP_NAME "TOPUP"
#define TOPUP_NAME_LEN 5U

int64_t
hook(uint32_t reserved)
{
    _g(1, 1);

    int64_t tt = otxn_type();

#line 132
    if (tt != ttPAYMENT)
        DONE("xahaucards: not a Payment, passing.");

    uint8_t shop_acc[20];
    hook_account(SBUF(shop_acc));

    uint8_t buyer[20];
    otxn_field(SBUF(buyer), sfAccount);

#line 148
    if (BUFFER_EQUAL_20(shop_acc, buyer))
        DONE("xahaucards: outgoing payment, passing.");

    uint8_t topup[1];

#line 159
    if (otxn_param(SBUF(topup), TOPUP_NAME, TOPUP_NAME_LEN) != DOESNT_EXIST)
        DONE("xahaucards: top-up accepted.");

    int64_t flags = otxn_field(0, 0, sfFlags);

#line 183
    if (flags > 0 && (flags & tfPartialPayment))
        NOPE("xahaucards: partial payments are not accepted.");

    int64_t amount = otxn_field(0, 0, sfAmount);

#line 191
    if (amount < 0)
        DONE("xahaucards: not XAH, passing.");

    int64_t drops = amount & ~0x4000000000000000LL;

    uint8_t issuer_acc[20];

#line 217
    if (hook_param(SBUF(issuer_acc), "ISSUER", 6) != 20)
        NOPE("xahaucards: this hook was installed without an ISSUER parameter.");

    int64_t price = state_foreign(0, 0, SBUF(KEY_PRICE), SBUF(NS_SETTINGS), SBUF(issuer_acc));

#line 233
    if (price <= 0)
        NOPE("xahaucards: the pack price is not configured on the issuer.");

#line 240
    if (drops != price)
        NOPE("xahaucards: pay the exact pack price.");

    uint8_t theme[32];

#line 255
    if (otxn_param(SBUF(theme), "THEME", 5) != 2)
        NOPE("xahaucards: a THEME parameter of exactly two digits is required, e.g. 01.");

    uint8_t dkey[TKEY_THEME_LEN];
    dkey[0] = 'D';
    dkey[1] = theme[0];
    dkey[2] = theme[1];

    uint8_t shape[64];

#line 276
    if (state_foreign(SBUF(shape), SBUF(dkey), SBUF(NS_TABLE), SBUF(issuer_acc)) != SHAPE_LEN)
        NOPE("xahaucards: no such set — check the THEME parameter, or load the card table.");

    dkey[0] = SALE_KEY_KIND;

    uint8_t sale[1];

#line 304
    if (state_foreign(SBUF(sale), SBUF(dkey), SBUF(NS_TABLE), SBUF(issuer_acc)) != 1
        || sale[0] != SALE_OPEN)
        NOPE("xahaucards: that set is not on sale.");

    uint8_t acc_keylet[34];

#line 338
    if (util_keylet(SBUF(acc_keylet), KEYLET_ACCOUNT, buyer, 20, 0, 0, 0, 0) != 34)
        NOPE("xahaucards: could not derive the buyer's account keylet.");

    if (slot_set(SBUF(acc_keylet), 1) != 1)
        NOPE("xahaucards: could not load the buyer's account.");

    int64_t acc_flags = slot_subfield(1, sfFlags, 2) == 2 ? slot(0, 0, 2) : 0;

#line 349
    if (acc_flags > 0 && (acc_flags & (lsfDisallowIncomingRemit | lsfRequireDestTag | lsfDepositAuth)))
        NOPE("xahaucards: your account is blocking incoming Remits, requires a destination tag or has deposit authorisation on — clear that and buy again.");

    int64_t budget = state_foreign(0, 0, SBUF(KEY_BUDGET), SBUF(NS_SETTINGS), SBUF(issuer_acc));

#line 356
    if (budget <= 0)
        NOPE("xahaucards: the mint budget is not configured on the issuer.");

#line 369
    if (budget >= price)
        NOPE("xahaucards: the mint budget is not less than the pack price — check the settings.");

#line 379
    if (etxn_reserve(1) != 1)
        NOPE("xahaucards: could not reserve the trigger emission.");

    uint8_t txid[32];

#line 389
    if (otxn_id(SBUF(txid), 0) != 32)
        NOPE("xahaucards: could not read the transaction id.");

    uint8_t intent[INTENT_LEN];

    BLIT20(intent + INTENT_BUYER_AT, buyer);
    intent[INTENT_THEME_AT] = theme[0];
    intent[INTENT_THEME_AT + 1U] = theme[1];
    intent[INTENT_STATUS_AT] = INTENT_PENDING;

#line 409
    if (state_set(SBUF(intent), SBUF(txid)) < 0)
        NOPE("xahaucards: could not record the purchase.");

    fire_trigger(shop_acc, issuer_acc, (uint64_t)budget);

#line 416
    DONE("xahaucards: paid — your cards are being minted.");
}
