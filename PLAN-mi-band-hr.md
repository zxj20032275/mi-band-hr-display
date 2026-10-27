# 小米手环心率显示应用 — 开发方案

## 1. 需求与确认结论

- 目标：FoloToy AI Passport（ESP32-C3，8MB Flash，无 PSRAM，ESP-IDF 5.5.3，240×320 ST7789P3，三键 UP/DOWN/OK）。
- 功能：蓝牙接收并显示小米手环实时心率；上下键调亮度；OK 键开始/结束接收；顶部居中显示持续时间；中部白色大字体心率；底部三键操作提示；背景色随心率区间变换。
- 已确认决策：
  1. 手环为 **小米手环 9 Pro / 10（Share HR）→ 标准 BLE 心率服务（HRS 0x180D）连接路径**（扫描→连接→发现服务→订阅 0x2A37 通知）。
  2. 心率区间配色采用五区间方案（见 §3）。
  3. OK 每次开始都**重新计时**（会话计时清零）。
  4. 亮度通过 **NVS 记忆**，开机恢复（步进 10%，范围 10%–100%）。

## 2. 蓝牙协议路径（Central + HRS）

NimBLE 主机流程（参考 main/demo_ble.c 的自管 host task 生命周期，改为中心角色）：

1. `nimble_port_init`，初始化 GAP/GATT 服务；host sync 后开始扫描 `ble_gap_disc`（主动扫描，不过滤）。
2. 扫描结果用**纯解析器**检查广播中 16-bit Service UUID 列表是否含 `0x180D`（HRS），首个符合设备作为目标（锁定地址）。
3. `ble_gap_connect` 建立连接；`ble_gattc_exchange_mtu`。
4. 按 UUID 发现：HRS 服务 `0x180D` → Heart Rate Measurement 特征 `0x2A37`（具备 NOTIFY）。
5. 向其 CCCD（特征值句柄+1）写 `0x0001` 订阅通知。
6. `BLE_GAP_EVENT_NOTIFY_RX` 中按 HRS 规范解析：
   - byte0 flags，bit0：0=uint8 心率；1=uint16（小端）；其后为心率值；其余能量/RR 字段忽略。
7. 活动期间意外断开：自动重新扫描并回连（UI 显示“重连中”）。
8. OK 结束：退订（尽力）→ `ble_gap_terminate` → 停止并反初始化 NimBLE，冻结时间与最后心率。

配置变更（sdkconfig.defaults）：
`CONFIG_BT_NIMBLE_ROLE_CENTRAL=y`、`CONFIG_BT_NIMBLE_ROLE_OBSERVER=y`、`CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=n`、`CONFIG_BT_NIMBLE_ROLE_BROADCASTER=n`；保留 MAX_CONNECTIONS=1、NVS_PERSIST=n。

## 3. 心率区间与背景色

| 区间 | 心率范围(bpm) | 背景色 | 含义 |
| --- | --- | --- | --- |
| IDLE | 无有效数据 | `#101828` 深墨蓝 | 待机/搜索/无信号 |
| Z1 | < 100 | `#1E88E5` 青蓝 | 热身/安静 |
| Z2 | 100–124 | `#2E9E5B` 翠绿 | 燃脂 |
| Z3 | 125–149 | `#E8B426` 琥珀黄 | 有氧 |
| Z4 | 150–169 | `#E8742C` 橙 | 无氧 |
| Z5 | ≥ 170 | `#D7263D` 红 | 极限 |

边界与颜色集中在 `main/hr_zones.*`，便于调整；数字始终白色保证对比度。

## 4. UI 设计（全新设计，不复用 demo 菜单/ui_pixel 外壳）

单屏应用，开机直接进入心率界面（240×320，圆角遮罩半径 30）：

- **顶部（y≈6–34）居中**：持续时间，Montserrat 24 白色；<1h 显示 `MM:SS`，≥1h 显示 `H:MM:SS`；待机时显示中文“待机”（16px 中文字体）。
- **左上角（y≈8）**：电量 `NN%`，Montserrat 14；读取失败（-1）不显示，不与居中时间重叠。
- **中部（y≈60–236）**：
  - 白色大字体（Noto 数字，约 104px）实时心率，居中；无数据显示 `--`。
  - 数字下方 `BPM`（Montserrat 14）。
  - 再下方连接状态（16px 中文）：`搜索中… / 连接中… / 已连接 / 已断开 重连中…`。
- **底部（y≈252–312）三行按键提示（16px 中文）**：
  - `上键 亮度+`
  - `下键 亮度-`
  - `OK 开始/停止`

## 5. 状态机

- `IDLE`（待机，蓝牙停止）→OK→ `CONNECTING`（计时清零，启动 NimBLE：扫描→连接→订阅）。
- `CONNECTING` ⇄ `RUNNING`：收到首条有效心率进入 RUNNING；断线回 CONNECTING 自动重连。
- 任意活动态 →OK→ `IDLE`：停止蓝牙，冻结时间与最后心率。
- UP/DOWN CLICK（任意状态）：亮度 ∓/±10%，立即生效并写 NVS。
- 活动中 5 秒无有效数据：大数字 `--`，背景 IDLE 色，计时继续。

## 6. 模块与文件（main/ 与 assets/）

纯逻辑（与 ESP-IDF/LVGL 解耦，可主机测试）：
- `main/hr_zones.c/.h`：区间判定 → 区间枚举 + 背景色。
- `main/hr_clock.c/.h`：秒数 → `MM:SS` / `H:MM:SS`。
- `main/hr_parse.c/.h`：① 广播 TLV 解析（是否含 0x180D）；② HRS Measurement 通知解析（uint8/uint16、有效性）。

应用与蓝牙：
- `main/hr_app.c/.h`：界面构建、状态机、LVGL 定时器、NVS 亮度存取、生命周期。
- `main/ble_hr.c/.h`：NimBLE 中心角色（init/deinit、扫描、连接、服务/特征发现、订阅、通知回调、自动重连）。
- `main/main.c`：重写为 BSP 初始化后直接启动心率应用（保留 BSP 初始化与失败处理模式）。
- `main/CMakeLists.txt`：更新源文件与依赖（nvs_flash、bt、esp_timer 等）。

字体（OFL 授权，生成过程可复现）：
- `assets/fonts/NotoSansCJKsc-Regular.otf`（已下载，SIL OFL 1.1）。
- 用 **lv_font_conv 1.5.3** 生成：
  - `hr_cn_16.c`：ASCII(0x20–0x7E) + 所需汉字（上下键亮度开始停止待机搜索中已连接断重）+ `… / + - / %`。
  - `hr_num_104.c`：数字 0–9 与 `-`（大心率显示）。
- 生成脚本 `assets/fonts/generate.bat`（记录字体、版本、范围、命令）与 `assets/fonts/README` 说明授权与字符清单。
- 启用 `CONFIG_LV_FONT_MONTSERRAT_24=y`（时间），保留 14（BPM/电量）。

## 7. 主机测试（tests/）

- `tests/test_hr_zones.c`：各边界（99/100、124/125、149/150、169/170）与无数据。
- `tests/test_hr_clock.c`：0、59、60、3599、3600、3661 等格式化。
- `tests/test_hr_parse.c`：
  - 广播：含/不含 0x180D、TLV 截断、UUID 完整/不完整列表。
  - HRS：flags=0 uint8；flags=1 uint16；截断；心率 0/0xFF 边界。
- 用 TCC 0.9.27（本机）编译运行；代码保持 C99 兼容，亦可用 gcc/clang。

## 8. 构建、验证与交付

- 主机测试：TCC 编译 3 组测试并运行。
- 固件：激活 ESP-IDF 5.5.3（C:\esp\esp-idf-v5.5.3）：
  `idf.py build` → `idf.py merge-bin -o build/...-full.bin` → `python tools/verify_firmware.py <build_dir>` 校验偏移/分区/合并一致性。
- 交付：可从 **0x0 刷写的合并固件 full.bin**（含 bootloader、分区表、factory app）。
- 报告分项：Build / Host tests / Device tests / Unverified。
- 未验证项（预期）：真机屏幕渲染与中文字形、与手环 9 Pro/10 的实际连接订阅（需手环开启 Share HR）、亮度实际观感、BLE 距离/共存与功耗。
- 交付后**主动询问是否刷机测试**，未经确认不烧录；不做整片擦除。

## 9. Git 工作流

- 保留工作区现状（当前干净）；从 `main` 新建 `feature/mi-band-hr-display` 分支开发。
- 不主动 commit/push（除非用户要求）；产物 bin 不入库。

## 10. 实施步骤

1. 完成 ESP-IDF 子模块拉取与工具安装（install esp32c3，走 dl.espressif.cn）。
2. 新建 feature 分支。
3. 生成字体 C 文件并验证字符覆盖。
4. 实现 hr_zones/hr_clock/hr_parse 纯逻辑 + 主机测试并跑通。
5. 实现 ble_hr 中心连接模块。
6. 实现 hr_app 界面/状态机/NVS，重写 main.c 与 CMake、sdkconfig.defaults。
7. idf.py 编译、修错、merge-bin、verify。
8. 交付合并固件与测试报告，询问刷机。
