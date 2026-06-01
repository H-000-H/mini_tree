#include "config_store.h"
#include "hal_storage.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * 配置源缓冲区基地址与大小.
 * 通过 config_store_bind_source() 在 config_store_init() 之前注入.
 * 不保留编译期链接符号, 新平台直接 bind_source() 即可.
 */
static const char* s_json_buffer = NULL;
static size_t      s_json_size   = 0;

void config_store_bind_source(const char* buffer, size_t size)
{
    s_json_buffer = buffer;
    s_json_size = size;
}

#define MAX_ENTRIES   32
#define BLOB_MAX      4096

#define FLAG_A_VALID  0xA0
#define FLAG_B_VALID  0x0B

typedef enum
{
    CS_TYPE_INT    = 0,
    CS_TYPE_FLOAT  = 1,
    CS_TYPE_BOOL   = 2,
    CS_TYPE_STRING = 3,
} cs_type_t;

typedef struct
{
    char     key[32];
    cs_type_t type;
    union
    {
        int   i;
        float f;
        bool  b;
        char  s[64];
    } value;
    bool dirty;
} cs_entry_t;

static cs_entry_t s_entries[MAX_ENTRIES];
static int        s_entry_count;
static int        s_health;
static bool       s_storage_ready;
static config_store_write_hook_t s_write_hook = NULL;

/*
 * 软件 CRC32 (多项式 0xEDB88320, 与 esp_rom_crc32_le 兼容)
 */
static uint32_t crc32_le(uint32_t crc, const uint8_t* buf, uint32_t len)
{
    static const uint32_t kTable[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
        0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
        0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
        0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
        0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
        0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
        0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
        0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
        0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
        0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
        0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
        0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
        0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
        0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
        0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
        0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
        0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
        0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
        0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
        0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
        0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
        0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
        0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
        0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
        0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
        0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
        0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
        0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
        0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
        0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
        0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
        0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
        0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
    };

    for (uint32_t i = 0; i < len; i++)
    {
        crc = kTable[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

static const char* find_json_value(const char* key)
{
    if (!s_json_buffer || !key) return NULL;
    const char* json = s_json_buffer;

    size_t key_len = strlen(key);
    if (key_len + 3 > 63) return NULL;

    char search_pat[64];
    search_pat[0] = '"';
    memcpy(search_pat + 1, key, key_len);
    search_pat[key_len + 1] = '"';
    search_pat[key_len + 2] = ':';
    search_pat[key_len + 3] = '\0';

    const char* found = strstr(json, search_pat);
    if (!found) return NULL;

    return found + key_len + 3;
}

static cs_entry_t* find_entry(const char* key)
{
    for (int i = 0; i < s_entry_count; i++)
    {
        if (strcmp(s_entries[i].key, key) == 0)
            return &s_entries[i];
    }
    return NULL;
}

static cs_entry_t* add_entry(const char* key, cs_type_t type)
{
    if (s_entry_count >= MAX_ENTRIES) return NULL;
    cs_entry_t* e = &s_entries[s_entry_count++];
    strncpy(e->key, key, sizeof(e->key) - 1);
    e->key[sizeof(e->key) - 1] = '\0';
    e->type  = type;
    e->dirty = false;
    memset(&e->value, 0, sizeof(e->value));
    return e;
}

static bool load_factory_defaults(void)
{
    if (!s_json_buffer || s_json_size == 0) return false;
    const char* json = s_json_buffer;
    size_t size = s_json_size;

    s_entry_count = 0;
    const char* p = json;
    while (p < json + size)
    {
        const char* key_start = strchr(p, '"');
        if (!key_start) break;
        key_start++;
        const char* key_end = strchr(key_start, '"');
        if (!key_end) break;

        size_t key_len = (size_t)(key_end - key_start);
        if (key_len >= 32)
        { p = key_end + 1; continue; }

        const char* colon = strchr(key_end, ':');
        if (!colon)
        { p = key_end + 1; continue; }

        const char* val_start = colon + 1;
        while (*val_start == ' ' || *val_start == '\t' || *val_start == '\n' || *val_start == '\r')
            val_start++;

        cs_type_t type;
        if (*val_start == '"')
        {
            type = CS_TYPE_STRING;
        }
        else if (*val_start == 't' || *val_start == 'f')
        {
            type = CS_TYPE_BOOL;
        }
        else if (strchr(val_start, '.') != NULL)
        {
            type = CS_TYPE_FLOAT;
        }
        else
        {
            type = CS_TYPE_INT;
        }

        char key_buf[32];
        memcpy(key_buf, key_start, key_len);
        key_buf[key_len] = '\0';

        cs_entry_t* e = add_entry(key_buf, type);
        if (e)
        {
            switch (type)
            {
            case CS_TYPE_BOOL:
                e->value.b = (strncmp(val_start, "true", 4) == 0);
                break;
            case CS_TYPE_INT:
                e->value.i = atoi(val_start);
                break;
            case CS_TYPE_FLOAT:
                e->value.f = (float)atof(val_start);
                break;
            case CS_TYPE_STRING: {
                const char* q1 = strchr(val_start, '"');
                const char* q2 = q1 ? strchr(q1 + 1, '"') : NULL;
                if (q1 && q2)
                {
                    size_t slen = (size_t)(q2 - q1 - 1);
                    if (slen >= sizeof(e->value.s)) slen = sizeof(e->value.s) - 1;
                    memcpy(e->value.s, q1 + 1, slen);
                    e->value.s[slen] = '\0';
                }
                break;
            }
            }
        }
        p = val_start + 1;
    }
    return true;
}

static bool blob_serialize(uint8_t* buf, size_t buf_size, size_t* out_len)
{
    size_t pos = 0;
    if (pos + 2 > buf_size) return false;
    buf[pos++] = (uint8_t)(s_entry_count & 0xFF);
    buf[pos++] = (uint8_t)((s_entry_count >> 8) & 0xFF);

    for (int i = 0; i < s_entry_count; i++)
    {
        cs_entry_t* e = &s_entries[i];
        uint8_t key_len = (uint8_t)strlen(e->key);
        if (pos + 1 + key_len + 1 + 8 > buf_size) return false;

        buf[pos++] = key_len;
        memcpy(buf + pos, e->key, key_len);
        pos += key_len;
        buf[pos++] = (uint8_t)e->type;

        switch (e->type)
        {
        case CS_TYPE_INT:
            buf[pos++] = (uint8_t)(e->value.i & 0xFF);
            buf[pos++] = (uint8_t)((e->value.i >> 8) & 0xFF);
            buf[pos++] = (uint8_t)((e->value.i >> 16) & 0xFF);
            buf[pos++] = (uint8_t)((e->value.i >> 24) & 0xFF);
            break;
        case CS_TYPE_FLOAT: {
            uint32_t bits;
            memcpy(&bits, &e->value.f, sizeof(bits));
            buf[pos++] = (uint8_t)(bits & 0xFF);
            buf[pos++] = (uint8_t)((bits >> 8) & 0xFF);
            buf[pos++] = (uint8_t)((bits >> 16) & 0xFF);
            buf[pos++] = (uint8_t)((bits >> 24) & 0xFF);
            break;
        }
        case CS_TYPE_BOOL:
            buf[pos++] = e->value.b ? 1 : 0;
            break;
        case CS_TYPE_STRING: {
            uint16_t slen = (uint16_t)strlen(e->value.s);
            buf[pos++] = (uint8_t)(slen & 0xFF);
            buf[pos++] = (uint8_t)((slen >> 8) & 0xFF);
            memcpy(buf + pos, e->value.s, slen);
            pos += slen;
            break;
        }
        }
    }
    *out_len = pos;
    return true;
}

static bool blob_deserialize(const uint8_t* buf, size_t len)
{
    if (len < 2) return false;

    size_t pos = 0;
    uint16_t count = (uint16_t)buf[pos] | ((uint16_t)buf[pos + 1] << 8);
    pos += 2;

    for (uint16_t i = 0; i < count; i++)
    {
        if (pos >= len) return false;
        uint8_t key_len = buf[pos++];
        if (pos + key_len > len || key_len >= 32) return false;

        char key[32];
        memcpy(key, buf + pos, key_len);
        key[key_len] = '\0';
        pos += key_len;

        if (pos >= len) return false;
        cs_type_t type = (cs_type_t)buf[pos++];

        cs_entry_t* e = add_entry(key, type);
        if (!e) continue;

        switch (type)
        {
        case CS_TYPE_INT:
            if (pos + 4 > len) return false;
            e->value.i = (int)buf[pos]
                       | ((int)buf[pos + 1] << 8)
                       | ((int)buf[pos + 2] << 16)
                       | ((int)buf[pos + 3] << 24);
            pos += 4;
            break;
        case CS_TYPE_FLOAT: {
            if (pos + 4 > len) return false;
            uint32_t bits = (uint32_t)buf[pos]
                          | ((uint32_t)buf[pos + 1] << 8)
                          | ((uint32_t)buf[pos + 2] << 16)
                          | ((uint32_t)buf[pos + 3] << 24);
            memcpy(&e->value.f, &bits, sizeof(e->value.f));
            pos += 4;
            break;
        }
        case CS_TYPE_BOOL:
            if (pos >= len) return false;
            e->value.b = (buf[pos++] != 0);
            break;
        case CS_TYPE_STRING: {
            if (pos + 2 > len) return false;
            uint16_t slen = (uint16_t)buf[pos] | ((uint16_t)buf[pos + 1] << 8);
            pos += 2;
            if (pos + slen > len) return false;
            if (slen >= sizeof(e->value.s)) slen = (uint16_t)(sizeof(e->value.s) - 1);
            memcpy(e->value.s, buf + pos, slen);
            e->value.s[slen] = '\0';
            pos += slen;
            break;
        }
        }
    }
    return true;
}

static uint8_t read_slot_flag(void)
{
    if (!s_storage_ready) return 0xFF;
    uint8_t flag = 0xFF;
    hal_storage_read_flag(&flag);
    return flag;
}

static bool write_slot_flag(uint8_t flag)
{
    if (!s_storage_ready) return false;
    return hal_storage_write_flag(flag);
}

static bool load_from_storage(uint8_t slot)
{
    uint8_t buf[BLOB_MAX];
    size_t len = BLOB_MAX;

    if (!hal_storage_read_blob(slot, buf, &len)) return false;
    if (len < 6) return false;

    uint32_t stored_crc;
    memcpy(&stored_crc, buf, sizeof(stored_crc));

    uint32_t calc_crc = crc32_le(0, buf + 4, (uint32_t)(len - 4));
    if (stored_crc != calc_crc)
    {
        uint8_t alt_slot = (slot == 0) ? 1 : 0;
        uint8_t alt_flag = (alt_slot == 0) ? FLAG_A_VALID : FLAG_B_VALID;
        if (read_slot_flag() != alt_flag)
        {
            if (load_from_storage(alt_slot))
            {
                write_slot_flag(alt_flag);
                s_health = 1;
                return true;
            }
        }
        s_health = -1;
        load_factory_defaults();
        return true;
    }

    return blob_deserialize(buf + 4, len - 4);
}

static bool save_to_storage(uint8_t slot)
{
    uint8_t buf[BLOB_MAX];
    size_t out_len = 0;
    if (!blob_serialize(buf + 4, BLOB_MAX - 4, &out_len)) return false;

    uint32_t crc = crc32_le(0, buf + 4, (uint32_t)out_len);
    memcpy(buf, &crc, sizeof(crc));
    out_len += 4;

    if (!hal_storage_write_blob(slot, buf, out_len)) return false;

    /* 直读验证 */
    uint8_t verify[BLOB_MAX];
    size_t verify_len = BLOB_MAX;
    if (!hal_storage_read_blob(slot, verify, &verify_len)) return false;
    if (verify_len != out_len) return false;
    if (memcmp(buf, verify, out_len) != 0) return false;

    return true;
}

bool config_store_init(void)
{
    s_storage_ready = hal_storage_init();

    if (!load_factory_defaults())
        return false;

    if (s_storage_ready)
    {
        uint8_t flag = read_slot_flag();
        if (flag == FLAG_A_VALID)
        {
            load_from_storage(0);
        }
        else if (flag == FLAG_B_VALID)
        {
            load_from_storage(1);
        }
    }
    return true;
}

bool config_store_get_bool(const char* key, bool default_value)
{
    cs_entry_t* e = find_entry(key);
    if (e && e->type == CS_TYPE_BOOL) return e->value.b;

    const char* value = find_json_value(key);
    if (!value) return default_value;
    if (strstr(value, "true") == value || strstr(value, " true") == value)
        return true;
    if (strstr(value, "false") == value || strstr(value, " false") == value)
        return false;
    return default_value;
}

int config_store_get_int(const char* key, int default_value)
{
    cs_entry_t* e = find_entry(key);
    if (e && e->type == CS_TYPE_INT) return e->value.i;

    const char* value = find_json_value(key);
    return value ? atoi(value) : default_value;
}

float config_store_get_float(const char* key, float default_value)
{
    cs_entry_t* e = find_entry(key);
    if (e && e->type == CS_TYPE_FLOAT) return e->value.f;

    const char* value = find_json_value(key);
    return value ? (float)atof(value) : default_value;
}

const char* config_store_get_string(const char* key, const char* default_value)
{
    cs_entry_t* e = find_entry(key);
    if (e && e->type == CS_TYPE_STRING) return e->value.s;

    const char* value = find_json_value(key);
    if (!value) return default_value;

    const char* first_quote = strchr(value, '"');
    if (!first_quote) return default_value;
    return first_quote + 1;
}

bool config_store_set_bool(const char* key, bool value)
{
    cs_entry_t* e = find_entry(key);
    if (!e) e = add_entry(key, CS_TYPE_BOOL);
    if (!e) return false;
    e->type = CS_TYPE_BOOL;
    e->value.b = value;
    e->dirty = true;
    return true;
}

bool config_store_set_int(const char* key, int value)
{
    cs_entry_t* e = find_entry(key);
    if (!e) e = add_entry(key, CS_TYPE_INT);
    if (!e) return false;
    e->type = CS_TYPE_INT;
    e->value.i = value;
    e->dirty = true;
    return true;
}

bool config_store_set_float(const char* key, float value)
{
    cs_entry_t* e = find_entry(key);
    if (!e) e = add_entry(key, CS_TYPE_FLOAT);
    if (!e) return false;
    e->type = CS_TYPE_FLOAT;
    e->value.f = value;
    e->dirty = true;
    return true;
}

bool config_store_set_string(const char* key, const char* value)
{
    cs_entry_t* e = find_entry(key);
    if (!e) e = add_entry(key, CS_TYPE_STRING);
    if (!e) return false;
    e->type = CS_TYPE_STRING;
    strncpy(e->value.s, value, sizeof(e->value.s) - 1);
    e->value.s[sizeof(e->value.s) - 1] = '\0';
    e->dirty = true;
    return true;
}

void config_store_register_write_hook(config_store_write_hook_t hook)
{
    s_write_hook = hook;
}

bool config_store_commit(void)
{
    uint8_t buf[BLOB_MAX];
    size_t out_len = 0;
    if (!blob_serialize(buf, BLOB_MAX, &out_len)) return false;

    /* 优先使用注册的回调桥 */
    if (s_write_hook)
    {
        return s_write_hook(buf, out_len);
    }

    /* 默认 hal_storage A/B slot 路径 */
    if (!s_storage_ready) return false;

    uint8_t current = read_slot_flag();
    uint8_t target = (current == FLAG_A_VALID) ? 1 : 0;

    if (!save_to_storage(target)) return false;

    uint8_t new_flag = (target == 0) ? FLAG_A_VALID : FLAG_B_VALID;
    if (!write_slot_flag(new_flag)) return false;

    for (int i = 0; i < s_entry_count; i++)
        s_entries[i].dirty = false;

    return true;
}

bool config_store_factory_reset(void)
{
    if (!s_storage_ready)
    {
        s_entry_count = 0;
        return load_factory_defaults();
    }

    hal_storage_erase_all();

    s_entry_count = 0;
    s_health = 0;

    if (!load_factory_defaults()) return false;

    save_to_storage(0);
    write_slot_flag(FLAG_A_VALID);
    return true;
}

int config_store_health(void)
{
    return s_health;
}
