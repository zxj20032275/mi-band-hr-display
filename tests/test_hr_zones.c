// tests/test_hr_zones.c —— 心率区间判定与配色主机测试。
#include "hr_zones.h"

#include <stdio.h>

static int failures;

static void check_zone(int hr, bool valid, hr_zone_t expect)
{
    hr_zone_t got = hr_zone_classify(hr, valid);
    if (got != expect) {
        printf("FAIL classify hr=%d valid=%d: got %d expect %d\n",
               hr, (int)valid, (int)got, (int)expect);
        failures++;
    }
}

int main(void)
{
    // 无效/待机
    check_zone(0, false, HR_ZONE_IDLE);
    check_zone(0, true, HR_ZONE_IDLE);
    check_zone(255, true, HR_ZONE_IDLE);
    check_zone(29, true, HR_ZONE_IDLE);
    check_zone(231, true, HR_ZONE_IDLE);

    // 边界
    check_zone(30, true, HR_ZONE_WARMUP);
    check_zone(99, true, HR_ZONE_WARMUP);
    check_zone(100, true, HR_ZONE_FATBURN);
    check_zone(124, true, HR_ZONE_FATBURN);
    check_zone(125, true, HR_ZONE_AEROBIC);
    check_zone(149, true, HR_ZONE_AEROBIC);
    check_zone(150, true, HR_ZONE_ANAEROBIC);
    check_zone(169, true, HR_ZONE_ANAEROBIC);
    check_zone(170, true, HR_ZONE_PEAK);
    check_zone(230, true, HR_ZONE_PEAK);

    // 每个区间颜色两两不同,且 IDLE 色固定
    uint32_t c_idle = hr_zone_color(HR_ZONE_IDLE);
    uint32_t c1 = hr_zone_color(HR_ZONE_WARMUP);
    uint32_t c2 = hr_zone_color(HR_ZONE_FATBURN);
    uint32_t c3 = hr_zone_color(HR_ZONE_AEROBIC);
    uint32_t c4 = hr_zone_color(HR_ZONE_ANAEROBIC);
    uint32_t c5 = hr_zone_color(HR_ZONE_PEAK);
    if (c_idle != 0x101828u) { printf("FAIL idle color\n"); failures++; }
    uint32_t colors[] = { c_idle, c1, c2, c3, c4, c5 };
    for (int i = 0; i < 6; i++)
        for (int j = i + 1; j < 6; j++)
            if (colors[i] == colors[j]) {
                printf("FAIL duplicate color %d/%d = 0x%06X\n", i, j, colors[i]);
                failures++;
            }

    if (failures) { printf("test_hr_zones: %d FAILURES\n", failures); return 1; }
    printf("test_hr_zones: PASS\n");
    return 0;
}
