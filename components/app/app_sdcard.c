#include "app_sdcard.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "ff.h"

/*
 * 说明：
 * 1. TF 卡走 SDSPI，不是 SDMMC 4-bit。
 * 2. SPI2 总线默认已经由 spi2_init() 初始化好。
 * 3. 这里不再手工 spi_bus_add_device()。
 * 4. 挂载点固定为 "/0:"。
 */

static const char *TAG = "app_sdcard";

#define APP_SD_NUM_CS   GPIO_NUM_2

static sdmmc_card_t *s_card = NULL;
static bool s_sdcard_mounted = false;

esp_err_t app_sdcard_init(void)
{
    if (s_sdcard_mounted) {
        ESP_LOGW(TAG, "sdcard already mounted");
        return ESP_OK;
    }

    /* 如果之前残留了挂载状态，先清掉 */
    if (s_card != NULL) {
        esp_vfs_fat_sdcard_unmount(APP_SDCARD_MOUNT_POINT, s_card);
        s_card = NULL;
        s_sdcard_mounted = false;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 4 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    /* 使用官方默认 SDSPI host 配置 */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 1000;   /* 先低速排查，稳定后再提高 */

    /* 使用官方默认 SDSPI device 配置 */
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = SPI2_HOST;
    slot_config.gpio_cs = APP_SD_NUM_CS;
    slot_config.gpio_cd = GPIO_NUM_NC;
    slot_config.gpio_wp = GPIO_NUM_NC;
#if SOC_SDMMC_IO_POWER_EXTERNAL
    slot_config.gpio_int = GPIO_NUM_NC;
#endif

    ESP_LOGI(TAG, "mount sdcard via SDSPI...");
    ESP_LOGI(TAG, "host=%d, cs=%d, mount=%s",
             (int)host.slot, (int)slot_config.gpio_cs, APP_SDCARD_MOUNT_POINT);

    esp_err_t ret = esp_vfs_fat_sdspi_mount(APP_SDCARD_MOUNT_POINT,
                                            &host,
                                            &slot_config,
                                            &mount_config,
                                            &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdspi_mount failed: %s", esp_err_to_name(ret));
        s_card = NULL;
        s_sdcard_mounted = false;
        return ret;
    }

    s_sdcard_mounted = true;

    if (s_card != NULL) {
        sdmmc_card_print_info(stdout, s_card);
    }

    ESP_LOGI(TAG, "sdcard mount ok: %s", APP_SDCARD_MOUNT_POINT);
    return ESP_OK;
}

bool app_sdcard_is_mounted(void)
{
    return s_sdcard_mounted;
}

esp_err_t app_sdcard_save_file(const char *path, const uint8_t *data, size_t len)
{
    if (!s_sdcard_mounted) {
        ESP_LOGE(TAG, "sdcard not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    if (path == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "failed to open file for write: %s", path);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);

    if (written != len) {
        ESP_LOGE(TAG, "write incomplete: %u / %u",
                 (unsigned)written, (unsigned)len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "file saved: %s, len=%u", path, (unsigned)len);
    return ESP_OK;
}

esp_err_t app_sdcard_get_file_size(const char *path, size_t *out_size)
{
    if (path == NULL || out_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "stat failed: %s", path);
        return ESP_FAIL;
    }

    *out_size = (size_t)st.st_size;
    return ESP_OK;
}

esp_err_t app_sdcard_get_usage(size_t *out_total_kb, size_t *out_free_kb)
{
    if (!s_sdcard_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    FATFS *fs;
    DWORD free_clusters;

    FRESULT res = f_getfree("0:", &free_clusters, &fs);
    if (res != FR_OK) {
        ESP_LOGE(TAG, "f_getfree failed: %d", res);
        return ESP_FAIL;
    }

    size_t total_sectors = (fs->n_fatent - 2) * fs->csize;
    size_t free_sectors  = free_clusters * fs->csize;

    size_t total_kb = (total_sectors / 1024) * fs->ssize;
    size_t free_kb  = (free_sectors  / 1024) * fs->ssize;

    if (out_total_kb != NULL) {
        *out_total_kb = total_kb;
    }

    if (out_free_kb != NULL) {
        *out_free_kb = free_kb;
    }

    return ESP_OK;
}