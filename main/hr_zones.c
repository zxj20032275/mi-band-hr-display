// main/hr_zones.c —— 心率区间判定与背景色映射(纯逻辑)。
#include "hr_zones.h"

// 背景色(0xRRGGBB),与方案 §3 表格一致。
#define COLOR_IDLE       0x101828u  // 深墨蓝
#define COLOR_WARMUP     0x1E88E5u  // 青蓝
#define COLOR_FATBURN    0x2E9E5Bu  // 翠绿
#define COLOR_AEROBIC    0xE8B426u  // 琥珀黄
#define COLOR_ANAEROBIC  0xE8742Cu  // 橙
#define COLOR_PEAK       0xD7263Du  // 红

// 物理可信心率范围(bpm)。超出视为无效,避免 0/255 等异常值染色。
#define HR_PHYS_MIN  30
#define HR_PHYS_MAX  230

hr_zone_t hr_zone_classify(int hr, bool valid)
{
    if (!valid || hr < HR_PHYS_MIN || hr > HR_PHYS_MAX) return HR_ZONE_IDLE;
    if (hr <= HR_BOUND_WARMUP_MAX)    return HR_ZONE_WARMUP;
    if (hr <= HR_BOUND_FATBURN_MAX)   return HR_ZONE_FATBURN;
    if (hr <= HR_BOUND_AEROBIC_MAX)   return HR_ZONE_AEROBIC;
    if (hr <= HR_BOUND_ANAEROBIC_MAX) return HR_ZONE_ANAEROBIC;
    return HR_ZONE_PEAK;
}

uint32_t hr_zone_color(hr_zone_t zone)
{
    switch (zone) {
    case HR_ZONE_WARMUP:    return COLOR_WARMUP;
    case HR_ZONE_FATBURN:   return COLOR_FATBURN;
    case HR_ZONE_AEROBIC:   return COLOR_AEROBIC;
    case HR_ZONE_ANAEROBIC: return COLOR_ANAEROBIC;
    case HR_ZONE_PEAK:      return COLOR_PEAK;
    case HR_ZONE_IDLE:
    default:                return COLOR_IDLE;
    }
}
