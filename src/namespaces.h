#ifndef XAHAUCARDS_NAMESPACES_H
#define XAHAUCARDS_NAMESPACES_H

static const uint8_t NS_INTENT[32] = {
    0x9AU, 0xDCU, 0x51U, 0xBDU, 0x51U, 0xB9U, 0x80U, 0x5AU,
    0xEDU, 0x67U, 0xCAU, 0xE5U, 0xBEU, 0x65U, 0xCDU, 0x7DU,
    0xABU, 0xCAU, 0x94U, 0x92U, 0x56U, 0x0EU, 0x95U, 0xABU,
    0x36U, 0x5DU, 0x8EU, 0xF8U, 0x08U, 0xD6U, 0x11U, 0xD7U,
};

static const uint8_t NS_EDITIONS[32] = {
    0xC4U, 0x6EU, 0x1AU, 0x05U, 0xA0U, 0x3CU, 0x21U, 0x8FU,
    0x4DU, 0x47U, 0xB1U, 0x70U, 0x35U, 0x42U, 0x9FU, 0xD6U,
    0xD8U, 0xFEU, 0x6FU, 0x4FU, 0xDCU, 0x30U, 0xC1U, 0xA1U,
    0x47U, 0x39U, 0x26U, 0x3BU, 0xC0U, 0x0DU, 0x44U, 0x23U,
};

static const uint8_t NS_TABLE[32] = {
    0x28U, 0x02U, 0xC8U, 0x66U, 0x3CU, 0x08U, 0xE0U, 0xE9U,
    0x70U, 0x30U, 0x2AU, 0xE1U, 0x42U, 0x25U, 0xA2U, 0x63U,
    0x58U, 0xA6U, 0xD8U, 0xD0U, 0x7CU, 0x33U, 0x35U, 0x40U,
    0xE3U, 0xA7U, 0x3FU, 0x84U, 0x01U, 0xBEU, 0xF8U, 0xC1U,
};

static const uint8_t NS_SETTINGS[32] = {
    0xE5U, 0x12U, 0x44U, 0xA5U, 0xEFU, 0xDEU, 0xA2U, 0xD6U,
    0x2FU, 0x5FU, 0xEAU, 0xE1U, 0xBBU, 0x46U, 0x51U, 0xB1U,
    0x9BU, 0xB7U, 0x58U, 0x36U, 0xE3U, 0xCDU, 0x12U, 0xCAU,
    0x6BU, 0xF2U, 0xD1U, 0xEDU, 0x43U, 0x38U, 0x54U, 0x3DU,
};

static const uint8_t NS_ATTEST[32] = {
    0x63U, 0x20U, 0x03U, 0x6DU, 0x8EU, 0xC1U, 0x16U, 0x9EU,
    0x9FU, 0x2AU, 0x18U, 0xF1U, 0xDCU, 0xD0U, 0x5AU, 0xA1U,
    0x1FU, 0x26U, 0xDBU, 0xF1U, 0x5AU, 0x6AU, 0x25U, 0x0CU,
    0x5FU, 0x47U, 0xCBU, 0x08U, 0xEDU, 0x9FU, 0x29U, 0x26U,
};

static const uint8_t KEY_PRICE[5] = {'P', 'R', 'I', 'C', 'E'};

static const uint8_t KEY_BUDGET[6] = {'B', 'U', 'D', 'G', 'E', 'T'};

static const uint8_t KEY_SHOP[4] = {'S', 'H', 'O', 'P'};

static const uint8_t KEY_DOMAIN[6] = {'D', 'O', 'M', 'A', 'I', 'N'};

#define DOMAIN_MAX 64U

static const uint8_t KEY_CLAIM[5] = {'C', 'L', 'A', 'I', 'M'};

static const uint8_t KEY_NEWACCT[7] = {'N', 'E', 'W', 'A', 'C', 'C', 'T'};

#define DROPS_LEN 8U

#define INTENT_KEY_LEN 32U

#define INTENT_BUYER_AT 0U
#define INTENT_THEME_AT 20U
#define INTENT_STATUS_AT 22U
#define INTENT_LEN 23U

#define INTENT_PENDING 'P'

#define INTENT_FAILED 'F'

#define INTENT_GRANTED 'G'

#define INTENT_LAPSED 'L'

static const uint8_t KEY_GRANTS[6] = {'G', 'R', 'A', 'N', 'T', 'S'};

#define GRANTS_LEN 4U

#define GRANTS_MAX 100000U

static const uint8_t KEY_MINTCOUNT[9] = {'M', 'I', 'N', 'T', 'C', 'O', 'U', 'N', 'T'};

#define MINTCOUNT_LEN 4U

#define NAME_WIDTH 20
#define LABEL_AT 1

#define ROW_FACTION_AT (1U + NAME_WIDTH)
#define ROW_LEN (ROW_FACTION_AT + 4U)

#define LABEL_WIDTH 12
#define LABEL_LEN (1U + LABEL_WIDTH)

#define SHAPE_LEN 20U
#define BLOCK_LEN 4U

#define BLOCK_START(shape, r) \
    (((uint32_t)(shape)[(r) * BLOCK_LEN] << 8) | (uint32_t)(shape)[((r) * BLOCK_LEN) + 1U])

#define BLOCK_COUNT(shape, r) \
    (((uint32_t)(shape)[((r) * BLOCK_LEN) + 2U] << 8) | (uint32_t)(shape)[((r) * BLOCK_LEN) + 3U])

#define MAX_SUBJECT 999U

#define TKEY_THEME_LEN 3U
#define TKEY_SUBJECT_LEN 6U
#define FKEY_LEN 7U

#define RKEY_LEN 2U

#define SALE_KEY_KIND 'O'

#define SALE_OPEN 0x01U

#define ATTEST_KEY_LEN 20U

#define ATTEST_SLOT_DIGITS 2U

#define ATTEST_NAME_MAX 62U
#define ATTEST_VALUE_MAX (ATTEST_SLOT_DIGITS + ATTEST_NAME_MAX)

#define ATTEST_SLOT_MAX 99U

#define URI_CODE_AT 13
#define URI_CODE_LEN 17

#define URI_MIN_LEN (URI_CODE_AT + URI_CODE_LEN + 2)

static const uint8_t J_IMAGE[] = "\"image\":\"https://";
static const uint8_t J_CARD[] = "/card/";
static const uint8_t J_IMG_END[] = ".avif\",";

#define IMAGE_SLOT_LEN 3U

#define IMAGE_VALUE_MAX ((sizeof(J_IMAGE) - 1) + DOMAIN_MAX + (sizeof(J_CARD) - 1) \
    + URI_CODE_LEN + IMAGE_SLOT_LEN + (sizeof(J_IMG_END) - 1))

#endif
