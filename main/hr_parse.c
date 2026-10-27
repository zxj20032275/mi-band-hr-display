// main/hr_parse.c —— 蓝牙数据纯解析。
#include "hr_parse.h"

bool hr_adv_has_uuid16(const uint8_t *data, size_t len, uint16_t uuid)
{
    if (data == NULL || len == 0) return false;

    size_t i = 0;
    while (i < len) {
        uint8_t field_len = data[i];
        if (field_len == 0) break;                 // 终止
        // field_len 含 type 字节;结构需完整落在缓冲内。
        if ((size_t)field_len + 1 + i > len) break;

        uint8_t type = data[i + 1];
        const uint8_t *payload = &data[i + 2];
        size_t payload_len = field_len - 1;

        if (type == HR_AD_TYPE_UUID16_INCOMPLETE ||
            type == HR_AD_TYPE_UUID16_COMPLETE) {
            for (size_t k = 0; k + 1 < payload_len; k += 2) {
                uint16_t u = (uint16_t)payload[k] | ((uint16_t)payload[k + 1] << 8);
                if (u == uuid) return true;
            }
        }
        i += (size_t)field_len + 1;
    }
    return false;
}

hr_meas_t hr_parse_measurement(const uint8_t *data, size_t len)
{
    hr_meas_t out = { 0 };
    if (data == NULL || len < 2) return out;       // 至少 flags + 1 字节心率

    uint8_t flags = data[0];
    out.is_uint16 = (flags & 0x01) != 0;

    if (!out.is_uint16) {
        out.hr = data[1];
    } else {
        if (len < 3) return out;                   // 需要 2 字节心率
        out.hr = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
    }

    out.valid = out.hr > 0;
    return out;
}
