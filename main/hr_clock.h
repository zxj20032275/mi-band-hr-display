// main/hr_clock.h —— 会话持续时间格式化(纯逻辑,与硬件/UI 无关,可主机测试)。
#pragma once

#include <stddef.h>
#include <stdint.h>

// 把已持续的整秒数格式化为字符串:
//   sec < 3600      -> "MM:SS"
//   sec >= 3600     -> "H:MM:SS" (小时不补零,分秒补两位)
// 写入 buf(含结尾 NUL)。n 不足时安全截断。
void hr_clock_format(uint32_t sec, char *buf, size_t n);
