// tests/test_hr_clock.c —— 持续时间格式化主机测试。
#include "hr_clock.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(uint32_t sec, const char *expect)
{
    char buf[16];
    hr_clock_format(sec, buf, sizeof(buf));
    if (strcmp(buf, expect) != 0) {
        printf("FAIL %u: got \"%s\" expect \"%s\"\n", sec, buf, expect);
        failures++;
    }
}

int main(void)
{
    check(0, "00:00");
    check(5, "00:05");
    check(59, "00:59");
    check(60, "01:00");
    check(61, "01:01");
    check(599, "09:59");
    check(600, "10:00");
    check(3599, "59:59");
    check(3600, "1:00:00");
    check(3661, "1:01:01");
    check(36000, "10:00:00");

    // 小缓冲安全截断且有 NUL
    char tiny[4];
    hr_clock_format(600, tiny, sizeof(tiny));
    if (tiny[sizeof(tiny) - 1] != '\0') { printf("FAIL trunc NUL\n"); failures++; }

    if (failures) { printf("test_hr_clock: %d FAILURES\n", failures); return 1; }
    printf("test_hr_clock: PASS\n");
    return 0;
}
