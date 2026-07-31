#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "iic.h"
#include "spi.h"
#include "xl9555.h"
#include "lcd.h"
#include "led.h"

#include "app_runtime.h"

static const char *TAG = "main";

/* 保留原工程里的 I2C 句柄 */
i2c_obj_t i2c0_master;

/**
 * @brief NVS 初始化
 */
static void app_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
}

/**
 * @brief 板级外设初始化
 *
 * 这里只保留最基础、最通用的硬件初始化：
 * - LED
 * - I2C
 * - SPI
 * - XL9555
 * - LCD
 *
 * 其余和业务流程相关的初始化
 * （相机、TF卡、Wi-Fi、HTTP、人脸检测等）
 * 都交给 app_runtime_start() 管理。
 */
static void app_board_init(void)
{
    led_init();

    i2c0_master = iic_init(I2C_NUM_0);

    spi2_init();

    xl9555_init(i2c0_master);

    lcd_init();
}

/**
 * @brief 启动画面
 */
static void app_boot_splash(void)
{
    lcd_fill(0, 0, 320, 240, WHITE);
    lcd_show_string(0,   0, 240, 32, 32, "ESP32-S3", RED);
    lcd_show_string(0,  40, 240, 24, 24, "FACE CAMERA", RED);
    lcd_show_string(0,  80, 320, 16, 16, "runtime starting...", BLUE);
}

void app_main(void)
{
    ESP_LOGI(TAG, "app_main start");

    app_nvs_init();
    app_board_init();
    app_boot_splash();

    esp_err_t ret = app_runtime_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "app_runtime_start failed: %s", esp_err_to_name(ret));

        lcd_fill(0, 120, 320, 200, WHITE);
        lcd_show_string(0, 120, 320, 16, 16, "runtime start failed", RED);

        /* 这里不直接重启，保留现场，方便串口排查 */
        while (1)
        {
            LED_TOGGLE();
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    ESP_LOGI(TAG, "app_runtime_start ok");

    /*
     * 理论上 app_runtime_start() 已经创建并接管了所有后台任务：
     * - ui_task
     * - control_task
     * - capture_task
     * - time_task
     *
     * 所以 main 这里不再承担业务逻辑。
     * 保留一个轻量心跳循环即可，方便调试。
     */
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "main alive");
    }
}