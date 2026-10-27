// main/hr_app.h —— 小米手环心率显示应用:界面、状态机与按键处理。
#pragma once

#include "bsp_button.h"

// 构建并加载心率界面,恢复 NVS 中保存的亮度。需在 BSP 显示/LVGL 就绪后调用,
// 且在 LVGL 任务上下文或持有 bsp_lvgl_lock。
void hr_app_init(void);

// 按键事件(由输入任务在持有 bsp_lvgl_lock 时调用)。
void hr_app_key(bsp_btn_t btn, bsp_btn_ev_t ev);
