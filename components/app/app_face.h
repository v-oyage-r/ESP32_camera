#ifndef __APP_FACE_H
#define __APP_FACE_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    bool has_face;
    int face_count;
    int x1;
    int y1;
    int x2;
    int y2;
} app_face_result_t;

esp_err_t app_face_init(void);
bool app_face_is_inited(void);

bool app_face_detect_rgb565(uint16_t *rgb565_buf, int width, int height, app_face_result_t *out_result);

void app_face_draw_result_rgb565(uint16_t *rgb565_buf, int width, int height, const app_face_result_t *result);

/**
 * @brief 在 RGB565 图像上绘制时间戳水印
 *
 * @param rgb565_buf RGB565 图像缓冲区
 * @param width      图像宽
 * @param height     图像高
 * @param timestamp  时间戳字符串，例如 "2026-04-07 15:32:18"
 */
void app_face_draw_timestamp_rgb565(uint16_t *rgb565_buf, int width, int height, const char *timestamp);

#ifdef __cplusplus
}
#endif

#endif /* __APP_FACE_H */