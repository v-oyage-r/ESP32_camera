#ifndef __APP_SDCARD_H
#define __APP_SDCARD_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SDCARD_MOUNT_POINT "/0:"

esp_err_t app_sdcard_init(void);
bool app_sdcard_is_mounted(void);

esp_err_t app_sdcard_save_file(const char *path, const uint8_t *data, size_t len);
esp_err_t app_sdcard_get_file_size(const char *path, size_t *out_size);
esp_err_t app_sdcard_get_usage(size_t *out_total_kb, size_t *out_free_kb);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SDCARD_H */