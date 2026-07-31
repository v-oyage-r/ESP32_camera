#include "app_camera.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "xl9555.h"

/*
 * 说明：
 * 1. 这版按你现有板级文件的引脚定义整理，继续兼容 OV5640。
 * 2. PWDN / RESET 不走 ESP32-S3 原生 GPIO，而是走 XL9555。
 * 3. 当前目标是 HTTP 抓拍，因此输出格式改成 JPEG。
 */

static const char *TAG = "app_camera";
static bool s_camera_inited = false;

/* 摄像头引脚定义：沿用你原始 camera.h 的配置 */
#define CAM_PIN_PWDN     GPIO_NUM_NC
#define CAM_PIN_RESET    GPIO_NUM_NC
#define CAM_PIN_XCLK     GPIO_NUM_NC
#define CAM_PIN_SIOD     GPIO_NUM_39
#define CAM_PIN_SIOC     GPIO_NUM_38
#define CAM_PIN_D7       GPIO_NUM_18
#define CAM_PIN_D6       GPIO_NUM_17
#define CAM_PIN_D5       GPIO_NUM_16
#define CAM_PIN_D4       GPIO_NUM_15
#define CAM_PIN_D3       GPIO_NUM_7
#define CAM_PIN_D2       GPIO_NUM_6
#define CAM_PIN_D1       GPIO_NUM_5
#define CAM_PIN_D0       GPIO_NUM_4
#define CAM_PIN_VSYNC    GPIO_NUM_47
#define CAM_PIN_HREF     GPIO_NUM_48
#define CAM_PIN_PCLK     GPIO_NUM_45

/* 通过 XL9555 控制摄像头 PWDN / RESET */
#define CAM_PWDN(x)      do{ (x) ?                         \
                                xl9555_pin_write(OV_PWDN_IO, 1) :  \
                                xl9555_pin_write(OV_PWDN_IO, 0);   \
                            }while(0)

#define CAM_RST(x)       do{ (x) ?                         \
                                xl9555_pin_write(OV_RESET_IO, 1) : \
                                xl9555_pin_write(OV_RESET_IO, 0);  \
                            }while(0)

/* 当前改成更适合 HTTP 抓拍的 JPEG 配置 */
static camera_config_t s_camera_config = {
    .pin_pwdn       = CAM_PIN_PWDN,
    .pin_reset      = CAM_PIN_RESET,
    .pin_xclk       = CAM_PIN_XCLK,
    .pin_sccb_sda   = CAM_PIN_SIOD,
    .pin_sccb_scl   = CAM_PIN_SIOC,
    .pin_d7         = CAM_PIN_D7,
    .pin_d6         = CAM_PIN_D6,
    .pin_d5         = CAM_PIN_D5,
    .pin_d4         = CAM_PIN_D4,
    .pin_d3         = CAM_PIN_D3,
    .pin_d2         = CAM_PIN_D2,
    .pin_d1         = CAM_PIN_D1,
    .pin_d0         = CAM_PIN_D0,
    .pin_vsync      = CAM_PIN_VSYNC,
    .pin_href       = CAM_PIN_HREF,
    .pin_pclk       = CAM_PIN_PCLK,

    .xclk_freq_hz   = 24000000,
    .ledc_timer     = LEDC_TIMER_0,
    .ledc_channel   = LEDC_CHANNEL_0,

    .fb_location    = CAMERA_FB_IN_PSRAM,
    .pixel_format   = PIXFORMAT_RGB565,
    .frame_size     = FRAMESIZE_240X240,
    .jpeg_quality   = 12,
    .fb_count       = 1,
    .grab_mode      = CAMERA_GRAB_WHEN_EMPTY,
};

static void app_camera_hw_power_on(void)
{
    /*
     * 你原来的厂商代码在 pin 为 NC 时，反而通过 XL9555 控制电源/复位。
     * 这里沿用那个逻辑。
     */
    if (CAM_PIN_PWDN == GPIO_NUM_NC) {
        CAM_PWDN(0);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void app_camera_hw_reset(void)
{
    if (CAM_PIN_RESET == GPIO_NUM_NC) {
        CAM_RST(0);
        vTaskDelay(pdMS_TO_TICKS(20));
        CAM_RST(1);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void app_camera_sensor_tune(sensor_t *s)
{
    if (s == NULL) {
        return;
    }

    /*
     * 保留你原始文件里的兼容逻辑：
     * OV3660 / OV5640 做特定处理
     */
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
        s->set_saturation(s, -2);
        ESP_LOGI(TAG, "sensor detected: OV3660");
    } else if (s->id.PID == OV5640_PID) {
        s->set_vflip(s, 1);
        ESP_LOGI(TAG, "sensor detected: OV5640");
    } else {
        ESP_LOGI(TAG, "sensor detected, PID=0x%04x", s->id.PID);
    }
}

esp_err_t app_camera_init(void)
{
    if (s_camera_inited) {
        ESP_LOGW(TAG, "camera already initialized");
        return ESP_OK;
    }

    app_camera_hw_power_on();
    app_camera_hw_reset();

    esp_err_t err = esp_camera_init(&s_camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: %s", esp_err_to_name(err));
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "esp_camera_sensor_get failed");
        esp_camera_deinit();
        return ESP_FAIL;
    }

    app_camera_sensor_tune(s);

    s_camera_inited = true;
    ESP_LOGI(TAG, "camera init ok");
    return ESP_OK;
}

camera_fb_t *app_camera_capture(void)
{
    if (!s_camera_inited) {
        ESP_LOGE(TAG, "camera not initialized");
        return NULL;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        ESP_LOGE(TAG, "esp_camera_fb_get failed");
        return NULL;
    }

    /*
     * 当前配置是 JPEG，因此理论上 fb->format 应为 JPEG。
     * 这里不强制卡死，只打日志，便于后续排查。
     */
    if (fb->format != PIXFORMAT_JPEG) {
        ESP_LOGW(TAG, "captured frame is not JPEG, format=%d", fb->format);
    }

    return fb;
}

void app_camera_capture_done(camera_fb_t *fb)
{
    if (fb != NULL) {
        esp_camera_fb_return(fb);
    }
}