// main/hr_clock.c —— 会话持续时间格式化(纯逻辑)。
#include "hr_clock.h"

#include <stdio.h>

void hr_clock_format(uint32_t sec, char *buf, size_t n)
{
    if (buf == NULL || n == 0) return;

    uint32_t hours = sec / 3600;
    uint32_t minutes = (sec % 3600) / 60;
    uint32_t seconds = sec % 60;

    int written;
    if (hours > 0) {
        written = snprintf(buf, n, "%u:%02u:%02u",
                           (unsigned)hours, (unsigned)minutes, (unsigned)seconds);
    } else {
        written = snprintf(buf, n, "%02u:%02u",
                           (unsigned)minutes, (unsigned)seconds);
    }

    // snprintf 返回值 >= n 表示被截断;确保末尾 NUL(snprintf 已保证,此处兜底)。
    if (written < 0 || (size_t)written >= n) {
        buf[n - 1] = '\0';
    }
}
