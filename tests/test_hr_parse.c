// tests/test_hr_parse.c —— 广播 UUID 与 HRS Measurement 解析主机测试。
#include "hr_parse.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect_true(int cond, const char *msg)
{
    if (!cond) { printf("FAIL %s\n", msg); failures++; }
}

int main(void)
{
    // --- 广播 UUID 解析 ---
    // 完整 16-bit UUID 列表:len=4? field: [05][03][0D 18][?? ??]  含 180D + 另一个
    uint8_t adv1[] = {
        0x02, 0x01, 0x06,                         // flags
        0x05, 0x03, 0x0D, 0x18, 0x0F, 0x18,       // complete uuid: 180D,180F
        0x03, 0x02, 0xAA, 0xFE,                   // incomplete uuid: FEAA
    };
    expect_true(hr_adv_has_uuid16(adv1, sizeof(adv1), 0x180D), "adv complete has 180D");
    expect_true(hr_adv_has_uuid16(adv1, sizeof(adv1), 0x180F), "adv complete has 180F");
    expect_true(hr_adv_has_uuid16(adv1, sizeof(adv1), 0xFEAA), "adv incomplete has FEAA");
    expect_true(!hr_adv_has_uuid16(adv1, sizeof(adv1), 0x180A), "adv lacks 180A");

    // 截断/异常
    expect_true(!hr_adv_has_uuid16(NULL, 0, 0x180D), "null adv");
    uint8_t bad1[] = { 0x05, 0x03, 0x0D };       // 声明长度但缓冲不足
    expect_true(!hr_adv_has_uuid16(bad1, sizeof(bad1), 0x180D), "truncated field");
    uint8_t bad2[] = { 0x00 };                   // 立即终止
    expect_true(!hr_adv_has_uuid16(bad2, sizeof(bad2), 0x180D), "zero length");
    uint8_t odd[] = { 0x02, 0x03, 0x0D };        // UUID 列表只有 1 字节
    expect_true(!hr_adv_has_uuid16(odd, sizeof(odd), 0x180D), "odd uuid byte");

    // --- HRS Measurement 解析 ---
    uint8_t m8[] = { 0x00, 0x42 };               // flags=0, HR=66
    hr_meas_t r1 = hr_parse_measurement(m8, sizeof(m8));
    expect_true(r1.valid && r1.hr == 66 && !r1.is_uint16, "uint8 HR 66");

    uint8_t m16[] = { 0x01, 0x42, 0x01 };        // flags=1, HR=0x0142=322
    hr_meas_t r2 = hr_parse_measurement(m16, sizeof(m16));
    expect_true(r2.valid && r2.hr == 322 && r2.is_uint16, "uint16 HR 322");

    uint8_t mzero[] = { 0x00, 0x00 };            // HR=0 视为无效
    hr_meas_t r3 = hr_parse_measurement(mzero, sizeof(mzero));
    expect_true(!r3.valid, "HR zero invalid");

    expect_true(!hr_parse_measurement(NULL, 0).valid, "null meas");
    expect_true(!hr_parse_measurement(m8, 1).valid, "truncated flags");
    uint8_t t16[] = { 0x01, 0x42 };             // 声明 uint16 但缺字节
    expect_true(!hr_parse_measurement(t16, sizeof(t16)).valid, "truncated uint16");

    if (failures) { printf("test_hr_parse: %d FAILURES\n", failures); return 1; }
    printf("test_hr_parse: PASS\n");
    return 0;
}
