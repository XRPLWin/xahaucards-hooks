#include "hookapi.h"
#include "namespaces.h"

#define CARDS_PER_PACK 5U

#define CODE_LEN 17

#define MAX_EDITION_DIGITS 10U

#define URI_THEME_AT 16
#define URI_SUBJECT_AT 19
#define URI_RARITY_AT 23
#define URI_ATTACK_AT 25
#define URI_HEALTH_AT 28
#define URI_EDITION_AT 31

#define EMIT_DETAILS_LEN 116U

static const uint8_t J_HEAD[] = "{\"s\":\"xahaucards.card/v1\",\"name\":\"";

static const uint8_t J_NAME_END[] = "\",";
static const uint8_t J_ATTRS[] = "\"attributes\":[{\"trait_type\":\"Theme\",\"value\":\"";
static const uint8_t J_FACTION[] = "\"},{\"trait_type\":\"Faction\",\"value\":\"";
static const uint8_t J_RARITY[] = "\"},{\"trait_type\":\"Rarity\",\"value\":\"";
static const uint8_t J_ATTACK[] = "\"},{\"trait_type\":\"Attack\",\"value\":";
static const uint8_t J_HEALTH[] = "},{\"trait_type\":\"Health\",\"value\":";
static const uint8_t J_EDITION[] = "},{\"trait_type\":\"Edition\",\"value\":";
static const uint8_t J_TAIL[] = "}]}";

static const uint8_t SCHEMA[] = "{\"std\":\"xahau-nft-remark/v1\",\"meta\":{\"parts\":";

static const uint8_t SCHEMA_EXT[] = ",\"ext\":{\"CardSignature\":{\"std\":\"xahau-nft-signature/v1\"}}";

_Static_assert(
    (sizeof(SCHEMA) - 1) + 1 + 1 + (sizeof(SCHEMA_EXT) - 1) + 1 <= 192,
    "The schema remark has outgrown the single byte VL form its length is written in."
);

#define REMARK_MAX 256

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

#define APPEND(lit) { BLIT64(meta + m, (lit)); m += sizeof(lit) - 1; }

#define VL_LEN(len) \
{ \
    if ((len) <= 192) \
        rtxn[r++] = (uint8_t)(len); \
    else \
    { \
        rtxn[r++] = (uint8_t)(193 + (((len) - 193) >> 8)); \
        rtxn[r++] = (uint8_t)(((len) - 193) & 0xFF); \
    } \
}

#define REMARK_OPEN_FLAGS(flags) \
{ \
    rtxn[r++] = 0xE0U; rtxn[r++] = 0x61U; \
    rtxn[r++] = 0x22U; \
    rtxn[r++] = 0x00U; rtxn[r++] = 0x00U; rtxn[r++] = 0x00U; rtxn[r++] = (uint8_t)(flags); \
}

#define REMARK_OPEN() REMARK_OPEN_FLAGS(0x01U)

#define REMARK_OPEN_MUTABLE() REMARK_OPEN_FLAGS(0x00U)

static const uint8_t RARITY[5] = {'C', 'U', 'R', 'E', 'L'};

static const uint8_t BAND_LO[5] = {1, 5, 9, 13, 20};
static const uint8_t BAND_HI[5] = {4, 8, 12, 19, 20};

static uint8_t rtxn[2048] = {

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

_Static_assert(
    R_BASE
        + (7 + 2 + 1 + (sizeof(SCHEMA) - 1) + (sizeof(SCHEMA_EXT) - 1) + 3 + 2 + 1 + 6 + 1)
        + 4 * (7 + 2 + 2 + REMARK_MAX + 2 + 1 + 6 + 1)
        + 1
        <= sizeof(rtxn),
    "rtxn is too small for the widest SetRemarks this hook can build."
);

#define R_FLS_OUT (rtxn + 15U)
#define R_LLS_OUT (rtxn + 21U)
#define R_OBJECT_OUT (rtxn + 26U)
#define R_FEE_OUT (rtxn + 59U)
#define R_ACCOUNT_OUT (rtxn + 104U)
#define R_EMIT_AT 124U
#define R_EMIT_OUT (rtxn + R_EMIT_AT)

static uint8_t ttxn[300] = {

  0x12U, 0x00U, 0x5FU,
  0x22U, 0x80U, 0x00U, 0x00U, 0x00U,
  0x24U, 0x00U, 0x00U, 0x00U, 0x00U,
  0x20U, 0x1AU, 0x00U, 0x00U, 0x00U, 0x00U,
  0x20U, 0x1BU, 0x00U, 0x00U, 0x00U, 0x00U,
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
               0,0,0,0,
  0xE0U, 0x5CU,
  0x75U, 0x00U,

};

#define T_FLS_OUT (ttxn + 15U)
#define T_LLS_OUT (ttxn + 21U)
#define T_FEE_OUT (ttxn + 26U)
#define T_ACCOUNT_OUT (ttxn + 71U)
#define T_DEST_OUT (ttxn + 93U)
#define T_EMIT_AT 113U
#define T_EMIT_OUT (ttxn + T_EMIT_AT)

#define T_URI_LEN_AT 232U
#define T_URI_AT 233U

_Static_assert(
    R_EMIT_AT + EMIT_DETAILS_LEN == R_BASE - 2U,
    "the SetRemarks template does not have room for its EmitDetails."
);

_Static_assert(
    T_EMIT_AT + EMIT_DETAILS_LEN == 229U,
    "the Remit template does not have room for its EmitDetails."
);

static uint8_t meta[640];

int64_t
hook(uint32_t reserved)
{
    _g(1, 1);

#line 541
    if (otxn_type() != ttPAYMENT)
        DONE("xahaucards: not a Payment, passing.");

    uint8_t issuer_acc[20];
    hook_account(SBUF(issuer_acc));

    uint8_t sender[20];
    otxn_field(SBUF(sender), sfAccount);

    uint8_t parent[32];

#line 571
    if (otxn_slot(1) != 1)
        DONE("xahaucards: could not slot the transaction, passing.");

    if (slot_subfield(1, sfEmitDetails, 2) != 2)
        DONE("xahaucards: a direct payment, passing.");

    if (slot_subfield(2, sfEmitParentTxnID, 3) != 3)
        DONE("xahaucards: an emitted payment with no parent id, passing.");

#line 582
    if (slot(SBUF(parent), 3) != 32)
        DONE("xahaucards: could not read the parent transaction id, passing.");

    uint8_t shop_acc[20];

#line 591
    if (state_foreign(SBUF(shop_acc), SBUF(KEY_SHOP), SBUF(NS_SETTINGS), SBUF(issuer_acc)) != 20)
        NOPE("xahaucards: the shop account is not configured on the issuer.");

    if (!BUFFER_EQUAL_20(sender, shop_acc))
        DONE("xahaucards: an emitted payment from somewhere other than the shop, passing.");

    uint8_t intent[INTENT_LEN];

#line 621
    if (state_foreign(SBUF(intent), parent, INTENT_KEY_LEN, SBUF(NS_INTENT), SBUF(shop_acc))
            != (int64_t)INTENT_LEN)
        NOPE("xahaucards: no purchase is recorded for this trigger.");

    uint8_t buyer[20];
    BLIT20(buyer, intent + INTENT_BUYER_AT);

    uint8_t last_hash[32];

#line 638
    if (ledger_last_hash(SBUF(last_hash)) != 32)
        NOPE("xahaucards: could not read the last ledger hash.");

    uint8_t seed_in[64];

    BLIT32(seed_in, parent);
    BLIT32(seed_in + 32, last_hash);

    uint8_t seed[32];

    if (util_sha512h(SBUF(seed), SBUF(seed_in)) != 32)
        NOPE("xahaucards: could not derive the pack seed.");

    uint8_t dkey[TKEY_THEME_LEN];
    dkey[0] = 'D';
    dkey[1] = intent[INTENT_THEME_AT];
    dkey[2] = intent[INTENT_THEME_AT + 1U];

    uint8_t shape[64];

#line 660
    if (state_foreign(SBUF(shape), SBUF(dkey), SBUF(NS_TABLE), SBUF(issuer_acc)) != SHAPE_LEN)
        NOPE("xahaucards: that set is not loaded on the issuer.");

    uint8_t domain[DOMAIN_MAX];
    int64_t domain_len = state_foreign(SBUF(domain), SBUF(KEY_DOMAIN), SBUF(NS_SETTINGS), SBUF(issuer_acc));

#line 670
    if (domain_len < 1 || domain_len > (int64_t)DOMAIN_MAX)
        NOPE("xahaucards: the card domain is not configured on the issuer.");

#line 679
    if (etxn_reserve(CARDS_PER_PACK * 2U) != CARDS_PER_PACK * 2U)
        NOPE("xahaucards: could not reserve emissions.");

    uint32_t fls = (uint32_t)ledger_seq() + 1;
    uint32_t lls = fls + 4;

#line 693
    for (uint32_t idx = 0; GUARD(CARDS_PER_PACK), idx < CARDS_PER_PACK; ++idx)
    {

    uint8_t card_in[33];

    BLIT32(card_in, seed);
    card_in[32] = (uint8_t)idx;

    uint8_t roll[32];

#line 709
    if (util_sha512h(SBUF(roll), SBUF(card_in)) != 32)
        NOPE("xahaucards: could not derive a card roll.");

    uint8_t rarity = (uint8_t)((roll[0] >= 150U) + (roll[0] >= 220U)
                             + (roll[0] >= 246U) + (roll[0] >= 254U));

    uint32_t start = BLOCK_START(shape, rarity);
    uint32_t filled = BLOCK_COUNT(shape, rarity);

#line 737
    if (filled == 0)
        NOPE("xahaucards: that set has no cards of a rarity it can roll — re-run load-cards.");

    uint32_t subject = start + ((((uint32_t)roll[1] << 8) | (uint32_t)roll[2]) % filled);

#line 750
    if (subject > MAX_SUBJECT)
        NOPE("xahaucards: that set's block record runs past subject 999 — re-run load-cards.");

    uint8_t spread = BAND_HI[rarity] - BAND_LO[rarity] + 1U;

    uint8_t attack = BAND_LO[rarity]
        + (uint8_t)(((((uint32_t)roll[3] << 8) | (uint32_t)roll[4]) % spread));
    uint8_t health = BAND_LO[rarity]
        + (uint8_t)(((((uint32_t)roll[5] << 8) | (uint32_t)roll[6]) % spread));

    uint8_t uri[80];

    uri[0] = 'x'; uri[1] = 'a'; uri[2] = 'h'; uri[3] = 'a';
    uri[4] = 'u'; uri[5] = 'c'; uri[6] = 'a'; uri[7] = 'r';
    uri[8] = 'd'; uri[9] = 's'; uri[10] = ':'; uri[11] = '/';
    uri[12] = '/';

    uri[URI_CODE_AT + 0] = 'X';
    uri[URI_CODE_AT + 1] = 'C';
    uri[URI_CODE_AT + 2] = '-';
    uri[URI_CODE_AT + 3] = intent[INTENT_THEME_AT];
    uri[URI_CODE_AT + 4] = intent[INTENT_THEME_AT + 1U];
    uri[URI_CODE_AT + 5] = '-';

    uri[URI_SUBJECT_AT + 0] = '0' + (uint8_t)(subject / 100U);
    uri[URI_SUBJECT_AT + 1] = '0' + (uint8_t)((subject / 10U) % 10U);
    uri[URI_SUBJECT_AT + 2] = '0' + (uint8_t)(subject % 10U);

    uri[URI_SUBJECT_AT + 3] = '-';
    uri[URI_RARITY_AT] = RARITY[rarity];
    uri[URI_RARITY_AT + 1] = '-';

    uri[URI_ATTACK_AT + 0] = '0' + (attack / 10U);
    uri[URI_ATTACK_AT + 1] = '0' + (attack % 10U);

    uri[URI_ATTACK_AT + 2] = '-';

    uri[URI_HEALTH_AT + 0] = '0' + (health / 10U);
    uri[URI_HEALTH_AT + 1] = '0' + (health % 10U);

    uri[URI_CODE_AT + CODE_LEN] = '-';

    uint8_t *card = uri;

    int64_t minted = state(0, 0, card + URI_CODE_AT, CODE_LEN);
    uint32_t edition = minted > 0 ? (uint32_t)minted + 1 : 1;

    uint8_t counter[4];
    UINT32_TO_BUF(counter, edition);

#line 837
    if (state_set(SBUF(counter), card + URI_CODE_AT, CODE_LEN) < 0)
        NOPE("xahaucards: could not record the edition number.");

    uint8_t digits[MAX_EDITION_DIGITS];
    uint32_t left = edition;
    uint8_t count = 0;

#line 845
    do {
        digits[count++] = '0' + (uint8_t)(left % 10U);
        left /= 10U;
    } while (GUARD(MAX_EDITION_DIGITS), left > 0);

    for (uint8_t d = 0; GUARD(MAX_EDITION_DIGITS), d < count; ++d)
        card[URI_EDITION_AT + d] = digits[count - 1U - d];

    int64_t uri_len = (int64_t)URI_EDITION_AT + (int64_t)count;

    uint8_t tkey[TKEY_SUBJECT_LEN];
    tkey[0] = 'S';
    tkey[1] = card[URI_THEME_AT];
    tkey[2] = card[URI_THEME_AT + 1];
    tkey[3] = card[URI_SUBJECT_AT];
    tkey[4] = card[URI_SUBJECT_AT + 1];
    tkey[5] = card[URI_SUBJECT_AT + 2];

    uint8_t row[128];
    int64_t row_len = state_foreign(SBUF(row), SBUF(tkey), SBUF(NS_TABLE), SBUF(issuer_acc));

#line 877
    if (row_len != (int64_t)ROW_LEN)
        NOPE("xahaucards: subject not in the table — load the card table first.");

    int name_len = row[0];

#line 895
    if (name_len < 1 || name_len > NAME_WIDTH)
        NOPE("xahaucards: a subject row's name length is outside its field — re-run load-cards.");

    uint8_t fkey[FKEY_LEN];
    fkey[0] = 'F';
    fkey[1] = card[URI_THEME_AT];
    fkey[2] = card[URI_THEME_AT + 1];
    fkey[3] = row[ROW_FACTION_AT];
    fkey[4] = row[ROW_FACTION_AT + 1];
    fkey[5] = row[ROW_FACTION_AT + 2];
    fkey[6] = row[ROW_FACTION_AT + 3];

    uint8_t faction_name[32];
    int64_t faction_len = state_foreign(SBUF(faction_name), SBUF(fkey), SBUF(NS_TABLE), SBUF(issuer_acc));

    tkey[0] = 'T';
    uint8_t theme_name[32];
    int64_t theme_name_len = state_foreign(SBUF(theme_name), tkey, TKEY_THEME_LEN, SBUF(NS_TABLE), SBUF(issuer_acc));

    uint8_t rkey[2];
    rkey[0] = 'R';
    rkey[1] = card[URI_RARITY_AT];
    uint8_t rarity_name[32];
    int64_t rarity_len = state_foreign(SBUF(rarity_name), SBUF(rkey), SBUF(NS_TABLE), SBUF(issuer_acc));

#line 924
    if (theme_name_len < 1 || faction_len < 1 || rarity_len < 1)
        NOPE("xahaucards: theme, faction or rarity missing — load the card table first.");

#line 932
    if (theme_name_len != (int64_t)LABEL_LEN || faction_len != (int64_t)LABEL_LEN
        || rarity_len != (int64_t)LABEL_LEN
        || theme_name[0] < 1 || theme_name[0] > LABEL_WIDTH
        || faction_name[0] < 1 || faction_name[0] > LABEL_WIDTH
        || rarity_name[0] < 1 || rarity_name[0] > LABEL_WIDTH)
        NOPE("xahaucards: a theme, faction or rarity record is not the shape the table defines — re-run load-cards.");

    int m = 0;

    APPEND(J_HEAD);
    BLIT64(meta + m, row + LABEL_AT);            m += name_len;
    meta[m++] = ' ';
    meta[m++] = '#';
    BLIT64(meta + m, card + URI_EDITION_AT);      m += (int)uri_len - URI_EDITION_AT;
    APPEND(J_NAME_END);

    int image_at = m;

    APPEND(J_IMAGE);
    BLIT64(meta + m, domain);                     m += (int)domain_len;
    APPEND(J_CARD);
    BLIT64(meta + m, card + URI_CODE_AT);         m += URI_CODE_LEN;

    APPEND(J_IMG_END);

    int attrs_at = m;

    APPEND(J_ATTRS);
    BLIT64(meta + m, theme_name + LABEL_AT);     m += theme_name[0];
    APPEND(J_FACTION);
    BLIT64(meta + m, faction_name + LABEL_AT);   m += faction_name[0];
    APPEND(J_RARITY);
    BLIT64(meta + m, rarity_name + LABEL_AT);    m += rarity_name[0];
    APPEND(J_ATTACK);

    if (card[URI_ATTACK_AT] != '0')
        meta[m++] = card[URI_ATTACK_AT];
    meta[m++] = card[URI_ATTACK_AT + 1];

    APPEND(J_HEALTH);

    if (card[URI_HEALTH_AT] != '0')
        meta[m++] = card[URI_HEALTH_AT];
    meta[m++] = card[URI_HEALTH_AT + 1];

    APPEND(J_EDITION);
    BLIT64(meta + m, card + URI_EDITION_AT);      m += (int)uri_len - URI_EDITION_AT;
    APPEND(J_TAIL);

    int attrs_len = m - attrs_at;
    int parts = attrs_len > REMARK_MAX ? 4 : 3;

    uint8_t preimage[2 + 20 + 64];
    preimage[0] = 0x00U;
    preimage[1] = 0x55U;

    BLIT20(preimage + 2, issuer_acc);
    BLIT64(preimage + 22, card);

#line 1016
    if (util_sha512h(R_OBJECT_OUT, 32, preimage, 22 + (uint32_t)uri_len) != 32)
        NOPE("xahaucards: could not derive the object id.");

    BLIT20(R_ACCOUNT_OUT, issuer_acc);

    *((uint32_t *)(R_FLS_OUT)) = FLIP_ENDIAN_32(fls);
    *((uint32_t *)(R_LLS_OUT)) = FLIP_ENDIAN_32(lls);

    int r = R_BASE;

    REMARK_OPEN();
    rtxn[r++] = 0x70U;
    rtxn[r++] = 0x62U;

    int schema_len_at = r++;
    int schema_from = r;

    BLIT64(rtxn + r, SCHEMA);   r += sizeof(SCHEMA) - 1;
    rtxn[r++] = '0' + (uint8_t)parts;
    rtxn[r++] = '}';
    BLIT64(rtxn + r, SCHEMA_EXT);   r += sizeof(SCHEMA_EXT) - 1;
    rtxn[r++] = '}';

    rtxn[schema_len_at] = (uint8_t)(r - schema_from);

    rtxn[r++] = 0x70U;
    rtxn[r++] = 0x63U;
    rtxn[r++] = 6;
    rtxn[r++] = 's'; rtxn[r++] = 'c'; rtxn[r++] = 'h';
    rtxn[r++] = 'e'; rtxn[r++] = 'm'; rtxn[r++] = 'a';
    rtxn[r++] = 0xE1U;

    REMARK_OPEN();
    rtxn[r++] = 0x70U;
    rtxn[r++] = 0x62U;
    VL_LEN(image_at);

    BLIT64(rtxn + r, meta);
    BLIT64(rtxn + r + 64, meta + 64);
    r += image_at;

    rtxn[r++] = 0x70U;
    rtxn[r++] = 0x63U;
    rtxn[r++] = 6;
    rtxn[r++] = 'm'; rtxn[r++] = 'e'; rtxn[r++] = 't'; rtxn[r++] = 'a';
    rtxn[r++] = '.'; rtxn[r++] = '0';
    rtxn[r++] = 0xE1U;

    int image_len = attrs_at - image_at;

    REMARK_OPEN_MUTABLE();
    rtxn[r++] = 0x70U;
    rtxn[r++] = 0x62U;
    VL_LEN(image_len);

    BLIT64(rtxn + r, meta + image_at);
    BLIT64(rtxn + r + 64, meta + image_at + 64);
    r += image_len;

    rtxn[r++] = 0x70U;
    rtxn[r++] = 0x63U;
    rtxn[r++] = 6;
    rtxn[r++] = 'm'; rtxn[r++] = 'e'; rtxn[r++] = 't'; rtxn[r++] = 'a';
    rtxn[r++] = '.'; rtxn[r++] = '1';
    rtxn[r++] = 0xE1U;

    int len0 = attrs_len > REMARK_MAX ? REMARK_MAX : attrs_len;

    REMARK_OPEN();
    rtxn[r++] = 0x70U;
    rtxn[r++] = 0x62U;
    VL_LEN(len0);

    BLIT64(rtxn + r, meta + attrs_at);
    BLIT64(rtxn + r + 64, meta + attrs_at + 64);
    BLIT64(rtxn + r + 128, meta + attrs_at + 128);
    BLIT64(rtxn + r + 192, meta + attrs_at + 192);
    r += len0;

    rtxn[r++] = 0x70U;
    rtxn[r++] = 0x63U;
    rtxn[r++] = 6;
    rtxn[r++] = 'm'; rtxn[r++] = 'e'; rtxn[r++] = 't'; rtxn[r++] = 'a';
    rtxn[r++] = '.'; rtxn[r++] = '2';
    rtxn[r++] = 0xE1U;

    if (attrs_len > REMARK_MAX)
    {
        int len1 = attrs_len - REMARK_MAX;

        REMARK_OPEN();
        rtxn[r++] = 0x70U;
        rtxn[r++] = 0x62U;
        VL_LEN(len1);

        BLIT64(rtxn + r, meta + attrs_at + REMARK_MAX);
        BLIT64(rtxn + r + 64, meta + attrs_at + REMARK_MAX + 64);
        BLIT64(rtxn + r + 128, meta + attrs_at + REMARK_MAX + 128);
        BLIT64(rtxn + r + 192, meta + attrs_at + REMARK_MAX + 192);
        r += len1;

        rtxn[r++] = 0x70U;
        rtxn[r++] = 0x63U;
        rtxn[r++] = 6;
        rtxn[r++] = 'm'; rtxn[r++] = 'e'; rtxn[r++] = 't'; rtxn[r++] = 'a';
        rtxn[r++] = '.'; rtxn[r++] = '3';
        rtxn[r++] = 0xE1U;
    }

    rtxn[r++] = 0xF1U;

#line 1162
    if (etxn_details(R_EMIT_OUT, EMIT_DETAILS_LEN) != EMIT_DETAILS_LEN)
        NOPE("xahaucards: unexpected emit details length.");

    int64_t fee = etxn_fee_base(rtxn, r);

    if (fee < 0)
        NOPE("xahaucards: could not price the remarks.");

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

    if (emit(SBUF(emit_hash), rtxn, r) < 0)
        NOPE("xahaucards: could not emit the remarks.");

    BLIT20(T_ACCOUNT_OUT, issuer_acc);
    BLIT20(T_DEST_OUT, buyer);
    *((uint32_t *)(T_FLS_OUT)) = FLIP_ENDIAN_32(fls);
    *((uint32_t *)(T_LLS_OUT)) = FLIP_ENDIAN_32(lls);

    ttxn[T_URI_LEN_AT] = (uint8_t)uri_len;
    BLIT64(ttxn + T_URI_AT, uri);
    ttxn[T_URI_AT + (uint32_t)uri_len] = 0xE1U;

    uint32_t t_len = T_URI_AT + (uint32_t)uri_len + 1U;

#line 1204
    if (etxn_details(T_EMIT_OUT, EMIT_DETAILS_LEN) != EMIT_DETAILS_LEN)
        NOPE("xahaucards: unexpected emit details length.");

    int64_t tfee = etxn_fee_base(ttxn, t_len);

    if (tfee < 0)
        NOPE("xahaucards: could not price the mint.");

    uint8_t *tf = T_FEE_OUT;
    *tf++ = 0b01000000 + ((tfee >> 56) & 0b00111111);
    *tf++ = (tfee >> 48) & 0xFFU;
    *tf++ = (tfee >> 40) & 0xFFU;
    *tf++ = (tfee >> 32) & 0xFFU;
    *tf++ = (tfee >> 24) & 0xFFU;
    *tf++ = (tfee >> 16) & 0xFFU;
    *tf++ = (tfee >> 8) & 0xFFU;
    *tf++ = (tfee >> 0) & 0xFFU;

    if (emit(SBUF(emit_hash), ttxn, t_len) < 0)
        NOPE("xahaucards: could not mint a card.");
    }

    int64_t supply = state(0, 0, SBUF(KEY_MINTCOUNT));

    uint32_t issued = (supply > 0 ? (uint32_t)supply : 0) + CARDS_PER_PACK;

    uint8_t total[MINTCOUNT_LEN];
    UINT32_TO_BUF(total, issued);

    state_set(SBUF(total), SBUF(KEY_MINTCOUNT));

#line 1268
    DONE("xahaucards: pack minted.");
}
