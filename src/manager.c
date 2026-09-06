#include "hookapi.h"
#include "namespaces.h"

#define ADMIN_BATCH 7

#define ADMIN_DELETE 0

static int64_t
admin(void)
{
    uint8_t hook_acc[20];
    hook_account(SBUF(hook_acc));

    uint8_t sender[20];
    otxn_field(SBUF(sender), sfAccount);

#line 97
    if (!BUFFER_EQUAL_20(hook_acc, sender))
        NOPE("xahaucards: an Invoke here must carry ATTEST, or be signed by the issuer.");

#line 102
    uint8_t selector[1];
    if (otxn_param(SBUF(selector), "NS", 2) != 1)
        NOPE("xahaucards: admin invoke needs an NS parameter of E, T, S or A.");

    const uint8_t *ns = 0;

    if (selector[0] == 'T')
        ns = NS_TABLE;
    else if (selector[0] == 'S')
        ns = NS_SETTINGS;
    else if (selector[0] == 'A')
        ns = NS_ATTEST;
    else if (selector[0] != 'E')
        NOPE("xahaucards: NS must be E (editions), T (table), S (settings) or A (attestors).");

    int64_t written = 0;

    for (int i = 0; GUARD(ADMIN_BATCH), i < ADMIN_BATCH; ++i)
    {

        uint8_t kname[2];
        uint8_t vname[2];
        kname[0] = 'K';
        vname[0] = 'V';
        kname[1] = '0' + i;
        vname[1] = '0' + i;

        uint8_t key[32];
        int64_t klen = otxn_param(SBUF(key), SBUF(kname));

        if (klen == DOESNT_EXIST)
            continue;

#line 141
        if (klen < 1)
            NOPE("xahaucards: admin invoke has a key longer than 32 bytes.");

        uint8_t value[256];
        int64_t vlen = otxn_param(SBUF(value), SBUF(vname));

        if (vlen < 1)
            NOPE("xahaucards: admin invoke has a key with no value.");

        int64_t result = (vlen == 1 && value[0] == ADMIN_DELETE)
            ? state_foreign_set(0, 0, key, klen, ns, ns ? 32 : 0, hook_acc, ns ? 20 : 0)
            : state_foreign_set(value, vlen, key, klen, ns, ns ? 32 : 0, hook_acc, ns ? 20 : 0);

#line 157
        if (result < 0)
            NOPE("xahaucards: could not write the entry.");

        written++;
    }

    if (written == 0)
        NOPE("xahaucards: admin invoke carried no K0/V0 parameters.");

    DONE("xahaucards: table entries written.");
}

#define LT_URI_TOKEN 0x0055U

#define RADDR_MAX 35U

#define SIG_VALUE_MAX (36U + RADDR_MAX + 8U + 64U + 2U)

#define ATTEST_NAME_LEN 13U

#define NAME_VL 1U

#define REMARKS_PER_OBJECT 32U

#define BLIT64(dst, src) \
{ \
    uint64_t *d_ = (uint64_t *)(dst); \
    const uint64_t *s_ = (const uint64_t *)(src); \
    d_[0] = s_[0]; d_[1] = s_[1]; d_[2] = s_[2]; d_[3] = s_[3]; \
    d_[4] = s_[4]; d_[5] = s_[5]; d_[6] = s_[6]; d_[7] = s_[7]; \
}

#define BLIT32(dst, src) \
{ \
    uint64_t *d_ = (uint64_t *)(dst); \
    const uint64_t *s_ = (const uint64_t *)(src); \
    d_[0] = s_[0]; d_[1] = s_[1]; d_[2] = s_[2]; d_[3] = s_[3]; \
}

#define BLIT20(dst, src) \
{ \
    uint64_t *d_ = (uint64_t *)(dst); \
    const uint64_t *s_ = (const uint64_t *)(src); \
    d_[0] = s_[0]; d_[1] = s_[1]; \
    *(uint32_t *)((uint8_t *)(dst) + 16) = *(const uint32_t *)((const uint8_t *)(src) + 16); \
}

#define EMIT_DETAILS_LEN 116U

#define HEX_NIBBLE(n) ((uint8_t)((n) < 10 ? '0' + (n) : 'A' + (n) - 10))

#define PUT(lit) { BLIT64(rtxn + r, (lit)); r += (int)(sizeof(lit) - 1); }

static const uint8_t J_BY[] = "{\"s\":\"xahau-nft-signature/v1\",\"by\":\"";
static const uint8_t J_TX[] = "\",\"tx\":\"";
static const uint8_t J_END[] = "\"}";

static const uint8_t SIG_NAME[] = "CardSignature";

_Static_assert(
    (sizeof(J_BY) - 1) + RADDR_MAX + (sizeof(J_TX) - 1) + 64U + (sizeof(J_END) - 1)
        == SIG_VALUE_MAX,
    "SIG_VALUE_MAX disagrees with the fragments the value is built from."
);

static const uint8_t SAID_SIGNED[] = "xahaucards: card signed by ";

static uint8_t rtxn[1024] = {

  0x12U, 0x00U, 0x5EU,
  0x22U, 0x80U, 0x00U, 0x00U, 0x00U,
  0x24U, 0x00U, 0x00U, 0x00U, 0x00U,
  0x20U, 0x1AU, 0x00U, 0x00U, 0x00U, 0x00U,
  0x20U, 0x1BU, 0x00U, 0x00U, 0x00U, 0x00U,
  0x5EU, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0x68U, 0x40U, 0x00U, 0x00U, 0x00U, 0x00U,
               0x00U, 0x00U, 0x00U,
  0x73U, 0x21U, 0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,
  0x81U, 0x14U, 0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
               0,0,0,0,
  0xF0U, 0x61U,

};

#define R_BASE 242

#define R_FLS_OUT (rtxn + 15U)
#define R_LLS_OUT (rtxn + 21U)
#define R_OBJECT_OUT (rtxn + 26U)
#define R_FEE_OUT (rtxn + 59U)
#define R_ACCOUNT_OUT (rtxn + 104U)
#define R_EMIT_OUT (rtxn + 124U)

_Static_assert(
    R_BASE
        + (7 + 2 + 1 + SIG_VALUE_MAX + 2 + 1 + ATTEST_NAME_LEN + 1)
        + (7 + 2 + 1 + IMAGE_VALUE_MAX + 2 + 1 + 6 + 1)
        + 1
        <= sizeof(rtxn),
    "rtxn is too small for the SetRemarks this hook builds."
);

_Static_assert(
    R_BASE
        + (7 + 2 + 1 + SIG_VALUE_MAX + 2 + 1 + ATTEST_NAME_LEN + 1)
        + (7 + 2 + 1 + (sizeof(J_IMAGE) - 1) + DOMAIN_MAX + (sizeof(J_CARD) - 1)
           + URI_CODE_LEN + IMAGE_SLOT_LEN)
        + 64
        <= sizeof(rtxn),
    "rtxn is too small for the 64 byte overshoot PUT makes on the last fragment."
);

static int64_t
attest(const uint8_t *tokenid)
{
    uint8_t hook_acc[20];
    hook_account(SBUF(hook_acc));

    uint8_t sender[20];
    otxn_field(SBUF(sender), sfAccount);

    uint8_t roll[ATTEST_VALUE_MAX];
    int64_t rlen = state_foreign(
        SBUF(roll), sender, ATTEST_KEY_LEN,
        SBUF(NS_ATTEST), SBUF(hook_acc));

#line 495
    if (rlen < (int64_t)ATTEST_SLOT_DIGITS + 1)
        NOPE("xahaucards: this account is not on the attestor roll.");

    uint8_t slot_hi = roll[0];
    uint8_t slot_lo = roll[1];

#line 505
    if (slot_hi < '0' || slot_hi > '9' || slot_lo < '0' || slot_lo > '9')
        NOPE("xahaucards: the roll entry for this account has no slot digits.");

    uint8_t keylet[34];

#line 512
    if (util_keylet(SBUF(keylet), KEYLET_UNCHECKED, (uint32_t)tokenid, 32, 0, 0, 0, 0) != 34)
        NOPE("xahaucards: could not derive the token keylet.");

#line 518
    if (slot_set(SBUF(keylet), 1) != 1)
        NOPE("xahaucards: no such URIToken — check the id, or it has been burned.");

    if (slot_subfield(1, sfLedgerEntryType, 2) != 2 || slot(0, 0, 2) != LT_URI_TOKEN)
        NOPE("xahaucards: that object is not a URIToken.");

    if (slot_subfield(1, sfIssuer, 3) != 3)
        NOPE("xahaucards: that URIToken has no issuer.");

    uint8_t issuer[20];

    if (slot(SBUF(issuer), 3) != 20)
        NOPE("xahaucards: could not read the token issuer.");

#line 534
    if (!BUFFER_EQUAL_20(issuer, hook_acc))
        NOPE("xahaucards: that card was not issued here.");

    if (slot_subfield(1, sfRemarks, 4) == 4)
    {
        int64_t count = slot_count(4);

#line 562
        if (count >= (int64_t)REMARKS_PER_OBJECT)
            NOPE("xahaucards: this card carries no room for another signature.");

        for (int32_t i = 0; GUARD(REMARKS_PER_OBJECT), i < count; ++i)
        {
            if (slot_subarray(4, (uint32_t)i, 5) != 5)
                continue;

            if (slot_subfield(5, sfRemarkName, 6) != 6)
                continue;

            uint8_t name[24];

            if (slot(SBUF(name), 6) != (int64_t)ATTEST_NAME_LEN + NAME_VL)
                continue;

#line 608
            if (name[NAME_VL + 0] == 'C' && name[NAME_VL + 1] == 'a'
                && name[NAME_VL + 2] == 'r' && name[NAME_VL + 3] == 'd'
                && name[NAME_VL + 4] == 'S' && name[NAME_VL + 5] == 'i'
                && name[NAME_VL + 6] == 'g' && name[NAME_VL + 7] == 'n'
                && name[NAME_VL + 8] == 'a' && name[NAME_VL + 9] == 't'
                && name[NAME_VL + 10] == 'u' && name[NAME_VL + 11] == 'r'
                && name[NAME_VL + 12] == 'e')
                NOPE("xahaucards: this card has already been signed.");
        }
    }

#line 621
    if (etxn_reserve(1) != 1)
        NOPE("xahaucards: could not reserve the emission.");

    BLIT32(R_OBJECT_OUT, tokenid);
    BLIT20(R_ACCOUNT_OUT, hook_acc);

    uint32_t fls = (uint32_t)ledger_seq() + 1;
    uint32_t lls = fls + 4;
    *((uint32_t *)(R_FLS_OUT)) = FLIP_ENDIAN_32(fls);
    *((uint32_t *)(R_LLS_OUT)) = FLIP_ENDIAN_32(lls);

    int r = R_BASE;

    rtxn[r++] = 0xE0U; rtxn[r++] = 0x61U;
    rtxn[r++] = 0x22U;
    rtxn[r++] = 0x00U; rtxn[r++] = 0x00U; rtxn[r++] = 0x00U; rtxn[r++] = 0x01U;

    uint8_t txid[32];

#line 646
    if (otxn_id(SBUF(txid), 0) != 32)
        NOPE("xahaucards: could not read this transaction's id.");

    rtxn[r++] = 0x70U; rtxn[r++] = 0x62U;

    int value_len_at = r++;
    int value_from = r;

    uint8_t raddr[64];
    int64_t raddr_len = util_raddr(SBUF(raddr), sender, 20);

#line 665
    if (raddr_len < 1 || raddr_len > (int64_t)RADDR_MAX)
        NOPE("xahaucards: could not render the signer's address.");

    PUT(J_BY);

    BLIT64(rtxn + r, raddr);
    r += (int)raddr_len;

    PUT(J_TX);

#line 678
    for (int i = 0; GUARD(32), i < 32; ++i)
    {
        rtxn[r++] = HEX_NIBBLE(txid[i] >> 4);
        rtxn[r++] = HEX_NIBBLE(txid[i] & 0x0FU);
    }

    PUT(J_END);

    int value_len = r - value_from;

#line 692
    if (value_len < 1 || value_len > 192)
        NOPE("xahaucards: the signature value came out the wrong length.");

    rtxn[value_len_at] = (uint8_t)value_len;

    rtxn[r++] = 0x70U; rtxn[r++] = 0x63U;
    rtxn[r++] = (uint8_t)ATTEST_NAME_LEN;

    PUT(SIG_NAME);

    rtxn[r++] = 0xE1U;

    uint8_t domain[DOMAIN_MAX];
    int64_t domain_len = state_foreign(
        SBUF(domain), SBUF(KEY_DOMAIN), SBUF(NS_SETTINGS), SBUF(hook_acc));

#line 726
    if (domain_len < 1 || domain_len > (int64_t)DOMAIN_MAX)
        NOPE("xahaucards: the card domain is not configured — load the settings first.");

    if (slot_subfield(1, sfURI, 7) != 7)
        NOPE("xahaucards: that URIToken has no URI.");

    uint8_t uri[64];

#line 737
    if (slot(SBUF(uri), 7) < (int64_t)NAME_VL + URI_CODE_AT + URI_CODE_LEN)
        NOPE("xahaucards: that URIToken's URI is too short to hold a card code.");

    rtxn[r++] = 0xE0U; rtxn[r++] = 0x61U;
    rtxn[r++] = 0x22U;
    rtxn[r++] = 0x00U; rtxn[r++] = 0x00U; rtxn[r++] = 0x00U; rtxn[r++] = 0x01U;

    rtxn[r++] = 0x70U; rtxn[r++] = 0x62U;

    int image_len_at = r++;
    int image_from = r;

    PUT(J_IMAGE);

    BLIT64(rtxn + r, domain);
    r += (int)domain_len;

    PUT(J_CARD);

    BLIT64(rtxn + r, uri + NAME_VL + URI_CODE_AT);
    r += URI_CODE_LEN;

    rtxn[r++] = '-';
    rtxn[r++] = slot_hi;
    rtxn[r++] = slot_lo;

    PUT(J_IMG_END);

    int image_len = r - image_from;

#line 786
    if (image_len < 1 || image_len > 192)
        NOPE("xahaucards: the image value came out the wrong length.");

    rtxn[image_len_at] = (uint8_t)image_len;

    rtxn[r++] = 0x70U; rtxn[r++] = 0x63U;
    rtxn[r++] = 6;
    rtxn[r++] = 'm'; rtxn[r++] = 'e'; rtxn[r++] = 't'; rtxn[r++] = 'a';
    rtxn[r++] = '.'; rtxn[r++] = '1';

    rtxn[r++] = 0xE1U;
    rtxn[r++] = 0xF1U;

    if (etxn_details(R_EMIT_OUT, EMIT_DETAILS_LEN) != EMIT_DETAILS_LEN)
        NOPE("xahaucards: unexpected emit details length.");

    int64_t fee = etxn_fee_base(rtxn, r);

    if (fee < 0)
        NOPE("xahaucards: could not price the emission.");

    uint8_t *f = R_FEE_OUT;
    *f++ = 0b01000000 + ((fee >> 56) & 0b00111111);
    *f++ = (fee >> 48) & 0xFFU;
    *f++ = (fee >> 40) & 0xFFU;
    *f++ = (fee >> 32) & 0xFFU;
    *f++ = (fee >> 24) & 0xFFU;
    *f++ = (fee >> 16) & 0xFFU;
    *f++ = (fee >> 8) & 0xFFU;
    *f++ = (fee >> 0) & 0xFFU;

    uint8_t emit_hash[32];

#line 826
    if (emit(SBUF(emit_hash), rtxn, r) < 0)
        NOPE("xahaucards: could not emit the signature.");

    uint8_t said[128];
    int m = 0;

    BLIT64(said, SAID_SIGNED);
    m += (int)(sizeof(SAID_SIGNED) - 1);

    BLIT64(said + m, raddr);
    m += (int)raddr_len;

#line 846
    return accept(said, (uint32_t)m, __LINE__);
}

int64_t
hook(uint32_t reserved)
{
    _g(1, 1);

#line 863
    if (otxn_type() != ttINVOKE)
        DONE("xahaucards-manager: not an Invoke, passing.");

    uint8_t tokenid[64];
    int64_t idlen = otxn_param(SBUF(tokenid), "ATTEST", 6);

#line 892
    if (idlen != 32 && idlen != DOESNT_EXIST)
        NOPE("xahaucards: ATTEST must be a 32 byte URIToken id.");

    if (idlen == 32)
        return attest(tokenid);

    return admin();
}
