#include "app_runtime.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_sntp.h"
#include "img_converters.h"

#include "lcd.h"
#include "led.h"

#include "app_camera.h"
#include "app_sdcard.h"
#include "app_key.h"
#include "app_face.h"
#include "app_wifi.h"
#include "app_http.h"

static const char *TAG = "app_runtime";

/* =========================
 * Event bits
 * ========================= */
#define APP_EVT_WIFI_READY      (1 << 0)
#define APP_EVT_SD_READY        (1 << 1)
#define APP_EVT_TIME_SYNCED     (1 << 2)
#define APP_EVT_CAPTURE_MODE    (1 << 3)
#define APP_EVT_HTTP_READY      (1 << 4)

/* =========================
 * Task settings
 * ========================= */
#define APP_UI_TASK_STACK       3072
#define APP_UI_TASK_PRIO        2

#define APP_CTRL_TASK_STACK     2048
#define APP_CTRL_TASK_PRIO      3

#define APP_CAPTURE_TASK_STACK  4096
#define APP_CAPTURE_TASK_PRIO   4

#define APP_TIME_TASK_STACK     3072
#define APP_TIME_TASK_PRIO      1

#define APP_CAPTURE_INTERVAL_MS     10000
#define APP_TIME_SYNC_INTERVAL_MS   (10 * 60 * 1000)
#define APP_IMAGE_KEEP_SECONDS      (2 * 60 * 60)

#define WIFI_ID "xxxxx"
#define WIFI_PWD "Your_Password"

/* =========================
 * Static resources
 * ========================= */
static EventGroupHandle_t s_app_evt = NULL;
static SemaphoreHandle_t s_status_mutex = NULL;
static SemaphoreHandle_t s_camera_mutex = NULL;
static SemaphoreHandle_t s_sd_mutex = NULL;

static TaskHandle_t s_capture_task = NULL;
static TaskHandle_t s_ui_task = NULL;
static TaskHandle_t s_control_task = NULL;
static TaskHandle_t s_time_task = NULL;

static app_runtime_status_t s_status = {0};

static bool s_runtime_started = false;
static uint32_t s_face_index = 0;
static uint32_t s_noface_index = 0;

/* =========================
 * Helpers: status
 * ========================= */
static void status_lock(void)
{
    if (s_status_mutex) {
        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    }
}

static void status_unlock(void)
{
    if (s_status_mutex) {
        xSemaphoreGive(s_status_mutex);
    }
}

static void status_set_last_result(const char *fmt, ...)
{
    va_list ap;

    status_lock();

    va_start(ap, fmt);
    vsnprintf(s_status.last_result, sizeof(s_status.last_result), fmt, ap);
    va_end(ap);

    status_unlock();
}

static void status_set_ip(const char *ip)
{
    status_lock();
    snprintf(s_status.ip_str, sizeof(s_status.ip_str), "%s", ip ? ip : "");
    status_unlock();
}

static void status_set_time_str(const char *time_str)
{
    status_lock();
    snprintf(s_status.time_str, sizeof(s_status.time_str), "%s", time_str ? time_str : "");
    status_unlock();
}

static void status_set_mode(app_runtime_mode_t mode)
{
    status_lock();
    s_status.mode = mode;
    status_unlock();
}

static void status_set_flag_wifi(bool ready)
{
    status_lock();
    s_status.wifi_ready = ready;
    status_unlock();
}

static void status_set_flag_sd(bool ready)
{
    status_lock();
    s_status.sd_ready = ready;
    status_unlock();
}

static void status_set_flag_http(bool ready)
{
    status_lock();
    s_status.http_ready = ready;
    status_unlock();
}

static void status_set_flag_time(bool ready)
{
    status_lock();
    s_status.time_synced = ready;
    status_unlock();
}

static void status_set_flag_face(bool ready)
{
    status_lock();
    s_status.face_ready = ready;
    status_unlock();
}

/* =========================
 * Helpers: UI
 * ========================= */
static void ui_draw_static(void)
{
    lcd_fill(0, 0, 320, 240, WHITE);
    lcd_show_string(0,   0, 240, 24, 24, "ESP32-S3 FACE CAM", RED);
    lcd_show_string(0,  28, 240, 16, 16, "KEY0: toggle capture", BLUE);
}

static void ui_draw_line(int y, const char *text, uint16_t color)
{
    /* 只清这一行，不清整个状态区 */
    lcd_fill(0, y, 320, y + 18, WHITE);
    lcd_show_string(0, y, 320, 16, 16, (char *)text, color);
}

static void ui_refresh_status(void)
{
    static char last_line1[96] = {0};
    static char last_line2[96] = {0};
    static char last_line3[96] = {0};
    static char last_line4[96] = {0};
    static char last_line5[96] = {0};
    static char last_line6[96] = {0};
    static char last_line7[96] = {0};
    static char last_line8[96] = {0};

    app_runtime_status_t snap;
    char line1[96], line2[96], line3[96], line4[96];
    char line5[96], line6[96], line7[96], line8[96];

    app_runtime_get_status(&snap);

    snprintf(line1, sizeof(line1), "Time: %s", snap.time_str[0] ? snap.time_str : "--");
    snprintf(line2, sizeof(line2), "WiFi: %s", snap.wifi_ready ? "OK" : "NO");
    snprintf(line3, sizeof(line3), "IP  : %s", snap.ip_str[0] ? snap.ip_str : "--");
    snprintf(line4, sizeof(line4), "SD  : %s", snap.sd_ready ? "OK" : "NO");
    snprintf(line5, sizeof(line5), "HTTP: %s", snap.http_ready ? "OK" : "NO");
    snprintf(line6, sizeof(line6), "Face: %s", snap.face_ready ? "OK" : "NO");
    snprintf(line7, sizeof(line7), "Mode: %s", (snap.mode == APP_MODE_CAPTURE) ? "CAPTURE" : "IDLE");
    snprintf(line8, sizeof(line8), "Last: %s", snap.last_result[0] ? snap.last_result : "--");

    if (strcmp(line1, last_line1) != 0) {
        ui_draw_line(60, line1, BLUE);
        strcpy(last_line1, line1);
    }

    if (strcmp(line2, last_line2) != 0) {
        ui_draw_line(82, line2, BLUE);
        strcpy(last_line2, line2);
    }

    if (strcmp(line3, last_line3) != 0) {
        ui_draw_line(104, line3, BLUE);
        strcpy(last_line3, line3);
    }

    if (strcmp(line4, last_line4) != 0) {
        ui_draw_line(126, line4, BLUE);
        strcpy(last_line4, line4);
    }

    if (strcmp(line5, last_line5) != 0) {
        ui_draw_line(148, line5, BLUE);
        strcpy(last_line5, line5);
    }

    if (strcmp(line6, last_line6) != 0) {
        ui_draw_line(170, line6, BLUE);
        strcpy(last_line6, line6);
    }

    if (strcmp(line7, last_line7) != 0) {
        ui_draw_line(192, line7, RED);
        strcpy(last_line7, line7);
    }

    if (strcmp(line8, last_line8) != 0) {
        ui_draw_line(214, line8, BLUE);
        strcpy(last_line8, line8);
    }
}

/* =========================
 * Helpers: NTP / time
 * ========================= */
static bool sync_time_once(void)
{
    /* 北京时间 UTC+8，无夏令时 */
    setenv("TZ", "CST-8", 1);
    tzset();

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    for (int retry = 0; retry < 15; retry++) {
        time_t now = time(NULL);
        struct tm timeinfo = {0};
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_year >= (2025 - 1900)) {
            ESP_LOGI(TAG, "time sync ok");
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGW(TAG, "time sync timeout");
    return false;
}

static void update_time_string_now(void)
{
    time_t now = time(NULL);
    struct tm timeinfo = {0};
    char buf[32] = {0};

    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year < (2025 - 1900)) {
        snprintf(buf, sizeof(buf), "--");
    } else {
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }

    status_set_time_str(buf);
}

/* =========================
 * Helpers: capture/save
 * ========================= */
typedef struct
{
    FILE *fp;
    size_t total_written;
} jpg_file_stream_t;

static size_t jpg_write_cb(void *arg, size_t index, const void *data, size_t len)
{
    (void)index;

    jpg_file_stream_t *stream = (jpg_file_stream_t *)arg;
    if (stream == NULL || stream->fp == NULL || data == NULL || len == 0) {
        return 0;
    }

    size_t written = fwrite(data, 1, len, stream->fp);
    stream->total_written += written;
    return written;
}

static void make_capture_path(char *path, size_t len, bool has_face)
{
    time_t now = time(NULL);
    struct tm timeinfo = {0};

    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year >= (2025 - 1900)) {
        if (has_face) {
            snprintf(path, len, "/0:/F%02d%02d%02d.JPG",
                     timeinfo.tm_hour,
                     timeinfo.tm_min,
                     timeinfo.tm_sec);
        } else {
            snprintf(path, len, "/0:/N%02d%02d%02d.JPG",
                     timeinfo.tm_hour,
                     timeinfo.tm_min,
                     timeinfo.tm_sec);
        }
    } else {
        /* 若尚未对时成功，则退回递增编号 */
        if (has_face) {
            snprintf(path, len, "/0:/F%07lu.JPG", (unsigned long)(++s_face_index));
        } else {
            snprintf(path, len, "/0:/N%07lu.JPG", (unsigned long)(++s_noface_index));
        }
    }
}

static esp_err_t save_rgb565_frame_as_jpg(camera_fb_t *fb, const char *path, uint8_t quality)
{
    if (fb == NULL || path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "failed to open file for write: %s, errno=%d", path, errno);
        return ESP_FAIL;
    }

    jpg_file_stream_t stream = {
        .fp = fp,
        .total_written = 0,
    };

    bool ok = fmt2jpg_cb(fb->buf,
                         fb->len,
                         fb->width,
                         fb->height,
                         fb->format,
                         quality,
                         jpg_write_cb,
                         &stream);

    fclose(fp);

    if (!ok || stream.total_written == 0) {
        ESP_LOGE(TAG, "fmt2jpg_cb failed: %s", path);
        unlink(path);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "jpg saved: %s, bytes=%u", path, (unsigned)stream.total_written);
    return ESP_OK;
}

static void cleanup_old_images_locked(void)
{
    DIR *dir;
    struct dirent *entry;
    time_t now = time(NULL);

    dir = opendir(APP_SDCARD_MOUNT_POINT);
    if (dir == NULL) {
        ESP_LOGW(TAG, "open dir failed for cleanup");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        char path[128];
        struct stat st;

        size_t nlen = strlen(entry->d_name);
        if (nlen < 4) {
            continue;
        }

        const char *ext = &entry->d_name[nlen - 4];
        if (strcasecmp(ext, ".jpg") != 0) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", APP_SDCARD_MOUNT_POINT, entry->d_name);

        if (stat(path, &st) != 0) {
            continue;
        }

        if (st.st_mtime <= 0) {
            continue;
        }

        if ((now - st.st_mtime) > APP_IMAGE_KEEP_SECONDS) {
            ESP_LOGI(TAG, "delete old image: %s", path);
            unlink(path);
        }
    }

    closedir(dir);
}

static bool ensure_face_inited(void)
{
    if (app_face_is_inited()) {
        return true;
    }

    ESP_LOGI(TAG, "lazy init face detector...");
    esp_err_t ret = app_face_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "face detector init failed: %s", esp_err_to_name(ret));
        status_set_last_result("face init fail");
        return false;
    }

    status_set_flag_face(true);
    status_set_last_result("face detector ok");
    return true;
}

static void do_capture_detect_save_once(void)
{
    if (!ensure_face_inited()) {
        return;
    }

    if (s_camera_mutex) {
        xSemaphoreTake(s_camera_mutex, portMAX_DELAY);
    }

    camera_fb_t *fb = app_camera_capture();

    if (s_camera_mutex) {
        xSemaphoreGive(s_camera_mutex);
    }

    if (fb == NULL) {
        ESP_LOGE(TAG, "capture failed");
        status_set_last_result("capture failed");
        return;
    }

    app_face_result_t result;
    bool detect_ok = app_face_detect_rgb565((uint16_t *)fb->buf, fb->width, fb->height, &result);

    if (!detect_ok) {
        ESP_LOGE(TAG, "face detect failed");
        status_set_last_result("detect fail");
        app_camera_capture_done(fb);
        return;
    }

    if (result.has_face) {
        app_face_draw_result_rgb565((uint16_t *)fb->buf, fb->width, fb->height, &result);
    }

    /* 统一叠加北京时间时间戳水印 */
    {
        char watermark[32] = {0};
        time_t now = time(NULL);
        struct tm timeinfo = {0};

        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_year >= (2025 - 1900)) {
            strftime(watermark, sizeof(watermark), "%Y-%m-%d %H:%M:%S", &timeinfo);
            app_face_draw_timestamp_rgb565((uint16_t *)fb->buf, fb->width, fb->height, watermark);
        }
    }

    char path[64] = {0};
    make_capture_path(path, sizeof(path), result.has_face);

    if (s_sd_mutex) {
        xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    }

    esp_err_t ret = save_rgb565_frame_as_jpg(fb, path, 80);

    if (ret == ESP_OK) {
        cleanup_old_images_locked();
    }

    if (s_sd_mutex) {
        xSemaphoreGive(s_sd_mutex);
    }

    app_camera_capture_done(fb);

    if (ret == ESP_OK) {
        status_set_last_result("%s saved", result.has_face ? "face" : "noface");
    } else {
        status_set_last_result("save fail");
    }
}

/* =========================
 * Tasks
 * ========================= */
static void ui_task(void *arg)
{
    (void)arg;

    ui_draw_static();

    TickType_t last_wake = xTaskGetTickCount();
    TickType_t last_sync_tick = 0;

    while (1) {
        update_time_string_now();
        ui_refresh_status();

        EventBits_t bits = xEventGroupGetBits(s_app_evt);
        if (bits & APP_EVT_WIFI_READY) {
            TickType_t now_tick = xTaskGetTickCount();

            if (last_sync_tick == 0 ||
                (now_tick - last_sync_tick) >= pdMS_TO_TICKS(APP_TIME_SYNC_INTERVAL_MS)) {

                bool ok = sync_time_once();
                status_set_flag_time(ok);

                if (ok) {
                    xEventGroupSetBits(s_app_evt, APP_EVT_TIME_SYNCED);
                    status_set_last_result("time synced");
                } else {
                    status_set_last_result("time sync fail");
                }

                last_sync_tick = now_tick;
            }
        }

        /* 每秒刷新一次，和时间显示粒度一致 */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}

static void control_task(void *arg)
{
    (void)arg;

    while (1) {
        uint8_t key = app_key_scan(0);

        if (key == APP_KEY0_PRES) {
            app_runtime_mode_t mode = app_runtime_toggle_capture_mode();

            if (mode == APP_MODE_CAPTURE) {
                status_set_last_result("capture mode on");
            } else {
                status_set_last_result("capture mode off");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

static void capture_task(void *arg)
{
    (void)arg;

    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500));

        if (app_runtime_get_mode() != APP_MODE_CAPTURE) {
            continue;
        }

        static TickType_t last_capture_tick = 0;
        TickType_t now = xTaskGetTickCount();

        if ((now - last_capture_tick) >= pdMS_TO_TICKS(APP_CAPTURE_INTERVAL_MS)) {
            last_capture_tick = now;
            do_capture_detect_save_once();
        }
    }
}

static void time_task(void *arg)
{
    (void)arg;

    while (1) {
        EventBits_t bits = xEventGroupGetBits(s_app_evt);

        if (bits & APP_EVT_WIFI_READY) {
            bool ok = sync_time_once();
            if (ok) {
                xEventGroupSetBits(s_app_evt, APP_EVT_TIME_SYNCED);
                status_set_flag_time(true);
                status_set_last_result("time synced");
            } else {
                status_set_flag_time(false);
                status_set_last_result("time sync fail");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(APP_TIME_SYNC_INTERVAL_MS));
    }
}

/* =========================
 * Public API
 * ========================= */
esp_err_t app_runtime_start(void)
{
    if (s_runtime_started) {
        return ESP_OK;
    }

    s_app_evt = xEventGroupCreate();
    s_status_mutex = xSemaphoreCreateMutex();
    s_camera_mutex = xSemaphoreCreateMutex();
    s_sd_mutex = xSemaphoreCreateMutex();

    if (!s_app_evt || !s_status_mutex || !s_camera_mutex || !s_sd_mutex) {
        ESP_LOGE(TAG, "create sync objects failed");
        return ESP_FAIL;
    }

    memset(&s_status, 0, sizeof(s_status));
    s_status.mode = APP_MODE_IDLE;
    snprintf(s_status.last_result, sizeof(s_status.last_result), "booting...");

    /* 1. TF 卡 */
    if (app_sdcard_init() == ESP_OK) {
        xEventGroupSetBits(s_app_evt, APP_EVT_SD_READY);
        status_set_flag_sd(true);
        status_set_last_result("sd ok");
    } else {
        status_set_flag_sd(false);
        status_set_last_result("sd fail");
    }

    /* 2. 相机 */
    if (app_camera_init() == ESP_OK) {
        status_set_last_result("camera ok");
    } else {
        status_set_last_result("camera fail");
        ESP_LOGE(TAG, "camera init failed");
        return ESP_FAIL;
    }

    /* 3. Wi-Fi */
    app_wifi_config_t wifi_cfg = {
        .ssid = WIFI_ID,
        .password = WIFI_PWD,
        .max_retry = 20,
    };

    if (app_wifi_start(&wifi_cfg) == ESP_OK &&
        app_wifi_wait_connected(15000) == ESP_OK)
    {
        const char *ip = app_wifi_get_ip_str();
        xEventGroupSetBits(s_app_evt, APP_EVT_WIFI_READY);
        status_set_flag_wifi(true);
        status_set_ip(ip);
        status_set_last_result("wifi ok");
    }
    else
    {
        status_set_flag_wifi(false);
        status_set_last_result("wifi fail");
    }

    /* 4. HTTP */
    status_lock();
    bool wifi_ready = s_status.wifi_ready;
    status_unlock();

    if (wifi_ready) {
        esp_err_t ret = app_http_start();
        if (ret == ESP_OK) {
            xEventGroupSetBits(s_app_evt, APP_EVT_HTTP_READY);
            status_set_flag_http(true);
            status_set_last_result("http ok");
        } else {
            status_set_flag_http(false);
            status_set_last_result("http fail");
            ESP_LOGW(TAG, "http start failed: %s", esp_err_to_name(ret));
        }
    }

    /* 5. 创建任务 */
    if (xTaskCreate(ui_task, "ui_task", APP_UI_TASK_STACK, NULL, APP_UI_TASK_PRIO, &s_ui_task) != pdPASS) {
        ESP_LOGE(TAG, "create ui_task failed");
        return ESP_FAIL;
    }

    if (xTaskCreate(control_task, "control_task", APP_CTRL_TASK_STACK, NULL, APP_CTRL_TASK_PRIO, &s_control_task) != pdPASS) {
        ESP_LOGE(TAG, "create control_task failed");
        return ESP_FAIL;
    }

    if (xTaskCreate(capture_task, "capture_task", APP_CAPTURE_TASK_STACK, NULL, APP_CAPTURE_TASK_PRIO, &s_capture_task) != pdPASS) {
        ESP_LOGE(TAG, "create capture_task failed");
        return ESP_FAIL;
    }

    if (xTaskCreate(time_task, "time_task", APP_TIME_TASK_STACK, NULL, APP_TIME_TASK_PRIO, &s_time_task) != pdPASS) {
        ESP_LOGE(TAG, "create time_task failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t app_runtime_get_status(app_runtime_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    status_lock();
    memcpy(out_status, &s_status, sizeof(app_runtime_status_t));
    status_unlock();

    return ESP_OK;
}

app_runtime_mode_t app_runtime_get_mode(void)
{
    app_runtime_mode_t mode;

    status_lock();
    mode = s_status.mode;
    status_unlock();

    return mode;
}

esp_err_t app_runtime_set_mode(app_runtime_mode_t mode)
{
    status_set_mode(mode);

    if (mode == APP_MODE_CAPTURE) {
        xEventGroupSetBits(s_app_evt, APP_EVT_CAPTURE_MODE);
    } else {
        xEventGroupClearBits(s_app_evt, APP_EVT_CAPTURE_MODE);
    }

    return ESP_OK;
}

app_runtime_mode_t app_runtime_toggle_capture_mode(void)
{
    app_runtime_mode_t cur = app_runtime_get_mode();

    if (cur == APP_MODE_IDLE) {
        app_runtime_set_mode(APP_MODE_CAPTURE);
        return APP_MODE_CAPTURE;
    } else {
        app_runtime_set_mode(APP_MODE_IDLE);
        return APP_MODE_IDLE;
    }
}

esp_err_t app_runtime_trigger_capture_once(void)
{
    if (!s_capture_task) {
        return ESP_FAIL;
    }

    do_capture_detect_save_once();
    return ESP_OK;
}