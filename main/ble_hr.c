// main/ble_hr.c —— NimBLE 中心角色:扫描/连接/发现/订阅小米手环 HRS。
#include "ble_hr.h"
#include "demo_radio.h"
#include "hr_parse.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "os/os_mbuf.h"
#include <string.h>

static const char *TAG = "ble_hr";

#define BLE_STOP_TIMEOUT_MS 3000
#define HR_NOTIFY_BUF_MAX   64

typedef enum {
    BLE_HR_OFF = 0,
    BLE_HR_SCANNING,
    BLE_HR_CONNECTING,
    BLE_HR_DISCOVERING,
    BLE_HR_RUNNING,
} ble_hr_state_t;

static SemaphoreHandle_t s_host_stopped;
static TaskHandle_t s_host_task;
static volatile ble_hr_state_t s_state;
static volatile bool s_active;       // 用户希望保持接收(含自动重连)
static bool s_initialized;
static bool s_stop_in_progress;
static bool s_host_done;
static uint8_t s_addr_type;
static uint16_t s_conn;
static uint16_t s_cccd_handle;
static ble_addr_t s_target;
static bool s_target_pending;
static bool s_svc_found;
static bool s_chr_found;
static ble_hr_cb_t s_cb;
static void *s_cb_arg;

static int gap_event(struct ble_gap_event *event, void *arg);
static int on_chr(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_chr *chr, void *arg);
static int on_subscribe(uint16_t conn, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg);

static void emit(ble_hr_evt_t evt, uint16_t hr)
{
    if (s_cb) s_cb(evt, hr, s_cb_arg);
}

static int start_scan(void)
{
    struct ble_gap_disc_params params = { 0 };
    params.itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
    params.window = BLE_GAP_SCAN_FAST_WINDOW;
    params.passive = 0;               // 主动扫描,获取 scan response 中的 UUID
    params.filter_policy = BLE_HCI_CONN_FILT_NO_WL;
    params.limited = 0;
    params.filter_duplicates = 0;

    s_state = BLE_HR_SCANNING;
    emit(BLE_HR_EVT_SCANNING, 0);
    return ble_gap_disc(s_addr_type, BLE_HS_FOREVER, &params, gap_event, NULL);
}

static int connect_target(const ble_addr_t *addr)
{
    struct ble_gap_conn_params params = { 0 };
    params.scan_itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
    params.scan_window = BLE_GAP_SCAN_FAST_WINDOW;
    params.itvl_min = 0x0018;         // 30 ms
    params.itvl_max = 0x0028;         // 50 ms
    params.latency = 0;
    params.supervision_timeout = 400; // 4 s
    params.min_ce_len = 0;
    params.max_ce_len = 0;

    s_state = BLE_HR_CONNECTING;
    emit(BLE_HR_EVT_CONNECTING, 0);
    return ble_gap_connect(s_addr_type, addr, BLE_HS_FOREVER, &params,
                           gap_event, NULL);
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        // 未发现 HRS:断开该设备,回到扫描。
        if (!s_svc_found && s_active) {
            ESP_LOGW(TAG, "HRS not found, disconnect & rescan");
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }
    if (error->status != 0 || svc == NULL) return 0;

    s_svc_found = true;
    ble_uuid16_t chr_uuid;
    chr_uuid.u.type = BLE_UUID_TYPE_16;
    chr_uuid.value = HR_MEASURE_CHAR_UUID;
    return ble_gattc_disc_chrs_by_uuid(conn, svc->start_handle, svc->end_handle,
                                      &chr_uuid.u, on_chr, NULL);
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        if (!s_chr_found && s_active) {
            ESP_LOGW(TAG, "HR measurement chr not found, rescan");
            ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;
    }
    if (error->status != 0 || chr == NULL) return 0;
    if ((chr->properties & BLE_GATT_CHR_F_NOTIFY) == 0) return 0;

    s_chr_found = true;
    s_cccd_handle = chr->val_handle + 1;
    uint8_t cccd[2] = { 0x01, 0x00 };   // 订阅 Notifications
    return ble_gattc_write_flat(conn, s_cccd_handle, cccd, sizeof(cccd),
                                on_subscribe, NULL);
}

static int on_subscribe(uint16_t conn, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (error && error->status == 0 && s_active) {
        s_state = BLE_HR_RUNNING;
        emit(BLE_HR_EVT_CONNECTED, 0);
    } else if (error) {
        ESP_LOGW(TAG, "subscribe failed status=%d", error->status);
    }
    return 0;
}

static void begin_discovery(uint16_t conn)
{
    s_state = BLE_HR_DISCOVERING;
    s_svc_found = false;
    s_chr_found = false;
    ble_uuid16_t svc_uuid;
    svc_uuid.u.type = BLE_UUID_TYPE_16;
    svc_uuid.value = HR_SERVICE_UUID;
    int rc = ble_gattc_disc_svc_by_uuid(conn, &svc_uuid.u, on_svc, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "discover svc failed: %d", rc);
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (!s_active || s_state != BLE_HR_SCANNING) return 0;
        if (hr_adv_has_uuid16(event->disc.data, event->disc.length_data,
                              HR_SERVICE_UUID)) {
            // 记录目标并取消扫描;连接在 DISC_COMPLETE 后发起(NimBLE 推荐时序)。
            s_target = event->disc.addr;
            s_target_pending = true;
            ble_gap_disc_cancel();
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (!s_active) return 0;
        if (s_target_pending) {
            s_target_pending = false;
            int rc = connect_target(&s_target);
            if (rc != 0) {
                ESP_LOGW(TAG, "connect rc=%d, rescan", rc);
                return start_scan();
            }
            return 0;
        }
        if (s_state == BLE_HR_SCANNING) {
            return start_scan();
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0 || !s_active) {
            if (s_active) {
                ESP_LOGW(TAG, "connect failed status=%d rescan", event->connect.status);
                return start_scan();
            }
            return 0;
        }
        s_conn = event->connect.conn_handle;
        // MTU 交换完成由 BLE_GAP_EVENT_MTU 事件触发服务发现。
        return ble_gattc_exchange_mtu(s_conn, NULL, NULL);

    case BLE_GAP_EVENT_MTU:
        if (s_active) begin_discovery(event->mtu.conn_handle);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (!s_active) return 0;
        uint8_t flat[HR_NOTIFY_BUF_MAX];
        uint16_t total = 0;
        int rc = ble_hs_mbuf_to_flat(event->notify_rx.om, flat, sizeof(flat),
                                     &total);
        if (rc != 0 || total <= 2) return 0;
        // NimBLE 通知前 2 字节为属性句柄,其后为特征值。
        hr_meas_t m = hr_parse_measurement(flat + 2, total - 2);
        if (m.valid) {
            emit(BLE_HR_EVT_HR, m.hr);
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT:
        s_conn = 0;
        s_cccd_handle = 0;
        if (s_active) {
            emit(BLE_HR_EVT_DISCONNECTED, 0);
            return start_scan();
        }
        return 0;

    default:
        return 0;
    }
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "nimble reset reason=%d", reason);
    if (s_active) emit(BLE_HR_EVT_FAILED, 0);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0 && s_active) rc = start_scan();
    if (rc != 0) {
        ESP_LOGE(TAG, "sync start failed rc=%d", rc);
        emit(BLE_HR_EVT_FAILED, 0);
    }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    xSemaphoreGive(s_host_stopped);
    for (;;) vTaskSuspend(NULL);
}

esp_err_t ble_hr_start(ble_hr_cb_t cb, void *arg)
{
    if (s_initialized) return ESP_ERR_INVALID_STATE;

    s_cb = cb;
    s_cb_arg = arg;
    s_active = true;
    s_stop_in_progress = false;
    s_host_done = false;
    s_conn = 0;
    s_cccd_handle = 0;
    s_target_pending = false;
    s_svc_found = false;
    s_chr_found = false;

    esp_err_t err = demo_radio_nvs_prepare();
    if (err != ESP_OK) return err;

    err = nimble_port_init();
    if (err != ESP_OK) return err;
    s_initialized = true;

    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) {
        err = ESP_ERR_NO_MEM;
        goto failed_start;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;

    if (xTaskCreatePinnedToCore(host_task, "nimble_host", NIMBLE_HS_STACK_SIZE,
                                NULL, configMAX_PRIORITIES - 4, &s_host_task,
                                NIMBLE_CORE) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto failed_start;
    }
    return ESP_OK;

failed_start:
    s_active = false;
    esp_err_t cleanup = ble_hr_stop();
    if (cleanup != ESP_OK) {
        ESP_LOGE(TAG, "startup cleanup failed: %s", esp_err_to_name(cleanup));
    }
    return err;
}

esp_err_t ble_hr_stop(void)
{
    s_active = false;
    if (!s_initialized) return ESP_OK;

    if (s_host_task && !s_stop_in_progress) {
        (void)ble_gap_disc_cancel();
        if (s_conn) {
            (void)ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
        }
        int rc = nimble_port_stop();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGE(TAG, "nimble_port_stop: %d", rc);
            return ESP_FAIL;
        }
        s_stop_in_progress = true;
    }

    if (s_host_task && !s_host_done) {
        if (!s_host_stopped ||
            xSemaphoreTake(s_host_stopped, pdMS_TO_TICKS(BLE_STOP_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "wait host stop timeout");
        }
        s_host_done = true;
    }

    if (s_host_task) {
        vTaskDelete(s_host_task);
        s_host_task = NULL;
    }

    esp_err_t err = nimble_port_deinit();
    if (err != ESP_OK) return err;

    s_initialized = false;
    s_stop_in_progress = false;
    s_host_done = false;
    s_conn = 0;
    s_cccd_handle = 0;
    s_target_pending = false;
    s_svc_found = false;
    s_chr_found = false;
    if (s_host_stopped) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
    }
    s_state = BLE_HR_OFF;
    return ESP_OK;
}
