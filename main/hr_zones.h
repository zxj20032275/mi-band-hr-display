// main/hr_zones.h —— 心率区间判定与背景色映射(纯逻辑,与硬件/UI 无关,可主机测试)。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 心率区间。HR_ZONE_IDLE 表示无有效数据(待机/搜索/无信号)。
typedef enum {
    HR_ZONE_IDLE = 0,
    HR_ZONE_WARMUP,   // Z1 热身/安静
    HR_ZONE_FATBURN,  // Z2 燃脂
    HR_ZONE_AEROBIC,  // Z3 有氧
    HR_ZONE_ANAEROBIC,// Z4 无氧
    HR_ZONE_PEAK,     // Z5 极限
} hr_zone_t;

// 各区间上界(含),单位 bpm。与方案表格一一对应:
//   <100 热身;100..124 燃脂;125..149 有氧;150..169 无氧;>=170 极限。
#define HR_BOUND_WARMUP_MAX     99
#define HR_BOUND_FATBURN_MAX    124
#define HR_BOUND_AEROBIC_MAX    149
#define HR_BOUND_ANAEROBIC_MAX  169

// 依据有效心率判定区间。valid=false(或心率非物理值)时返回 HR_ZONE_IDLE。
hr_zone_t hr_zone_classify(int hr, bool valid);

// 区间对应的背景色(0xRRGGBB)。
uint32_t hr_zone_color(hr_zone_t zone);
