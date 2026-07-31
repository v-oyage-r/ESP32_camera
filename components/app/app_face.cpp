#include "app_face.h"

#include <list>
#include <cstring>

#include "esp_log.h"
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"

static const char *TAG = "app_face";
static bool s_face_inited = false;

/* 静态检测器 */
static HumanFaceDetectMSR01 *s_detector_stage1 = nullptr;
static HumanFaceDetectMNP01 *s_detector_stage2 = nullptr;

#define APP_FACE_BOX_COLOR_RGB565    0xF800  /* 红色 */
#define APP_FACE_BOX_THICKNESS       2

#define APP_FACE_TEXT_COLOR_RGB565   0xFFFF  /* 白色 */
#define APP_FACE_BG_COLOR_RGB565     0x0000  /* 黑色 */

static inline void app_face_draw_pixel_rgb565(uint16_t *buf, int w, int h, int x, int y, uint16_t color)
{
    if (buf == nullptr) {
        return;
    }

    if (x < 0 || x >= w || y < 0 || y >= h) {
        return;
    }

    buf[y * w + x] = color;
}

static void app_face_fill_rect_rgb565(uint16_t *buf, int w, int h,
                                      int x, int y, int rect_w, int rect_h,
                                      uint16_t color)
{
    if (buf == nullptr || rect_w <= 0 || rect_h <= 0) {
        return;
    }

    for (int yy = 0; yy < rect_h; yy++) {
        for (int xx = 0; xx < rect_w; xx++) {
            app_face_draw_pixel_rgb565(buf, w, h, x + xx, y + yy, color);
        }
    }
}

static void app_face_draw_rect_rgb565(uint16_t *buf, int w, int h,
                                      int x1, int y1, int x2, int y2,
                                      uint16_t color, int thickness)
{
    if (buf == nullptr) {
        return;
    }

    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }

    if (thickness < 1) {
        thickness = 1;
    }

    for (int t = 0; t < thickness; t++) {
        int left   = x1 - t;
        int right  = x2 + t;
        int top    = y1 - t;
        int bottom = y2 + t;

        for (int x = left; x <= right; x++) {
            app_face_draw_pixel_rgb565(buf, w, h, x, top, color);
            app_face_draw_pixel_rgb565(buf, w, h, x, bottom, color);
        }

        for (int y = top; y <= bottom; y++) {
            app_face_draw_pixel_rgb565(buf, w, h, left, y, color);
            app_face_draw_pixel_rgb565(buf, w, h, right, y, color);
        }
    }
}

/* 5x7 字模，只支持时间戳需要的字符 */
static const uint8_t FONT_5X7_DIGITS[10][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x1E,0x01,0x01,0x06,0x01,0x01,0x1E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}  /* 9 */
};

static const uint8_t FONT_5X7_DASH[7]  = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
static const uint8_t FONT_5X7_COLON[7] = {0x00,0x04,0x00,0x00,0x04,0x00,0x00};
static const uint8_t FONT_5X7_SPACE[7] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00};

static const uint8_t *app_face_get_font_5x7(char c)
{
    if (c >= '0' && c <= '9') {
        return FONT_5X7_DIGITS[c - '0'];
    }

    switch (c) {
        case '-':
            return FONT_5X7_DASH;
        case ':':
            return FONT_5X7_COLON;
        case ' ':
            return FONT_5X7_SPACE;
        default:
            return FONT_5X7_SPACE;
    }
}

static void app_face_draw_char_5x7_rgb565(uint16_t *buf, int w, int h,
                                          int x, int y, char c,
                                          uint16_t color, int scale)
{
    if (buf == nullptr || scale <= 0) {
        return;
    }

    const uint8_t *glyph = app_face_get_font_5x7(c);

    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col))) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        app_face_draw_pixel_rgb565(buf, w, h,
                                                   x + col * scale + sx,
                                                   y + row * scale + sy,
                                                   color);
                    }
                }
            }
        }
    }
}

static void app_face_draw_string_5x7_rgb565(uint16_t *buf, int w, int h,
                                            int x, int y, const char *str,
                                            uint16_t color, int scale)
{
    if (buf == nullptr || str == nullptr || scale <= 0) {
        return;
    }

    int cursor_x = x;
    while (*str) {
        app_face_draw_char_5x7_rgb565(buf, w, h, cursor_x, y, *str, color, scale);
        cursor_x += (5 * scale + scale);  /* 字宽 + 1 个缩放单位间距 */
        str++;
    }
}

esp_err_t app_face_init(void)
{
    if (s_face_inited) {
        ESP_LOGW(TAG, "face detector already initialized");
        return ESP_OK;
    }

    s_detector_stage1 = new HumanFaceDetectMSR01(0.3F, 0.3F, 10, 0.3F);
    s_detector_stage2 = new HumanFaceDetectMNP01(0.4F, 0.3F, 10);

    if (s_detector_stage1 == nullptr || s_detector_stage2 == nullptr) {
        ESP_LOGE(TAG, "create detector failed");
        return ESP_FAIL;
    }

    s_face_inited = true;
    ESP_LOGI(TAG, "face detector init ok");
    return ESP_OK;
}

bool app_face_is_inited(void)
{
    return s_face_inited;
}

bool app_face_detect_rgb565(uint16_t *rgb565_buf, int width, int height, app_face_result_t *out_result)
{
    if (!s_face_inited) {
        ESP_LOGE(TAG, "face detector not initialized");
        return false;
    }

    if (rgb565_buf == nullptr || width <= 0 || height <= 0 || out_result == nullptr) {
        ESP_LOGE(TAG, "invalid input");
        return false;
    }

    out_result->has_face = false;
    out_result->face_count = 0;
    out_result->x1 = 0;
    out_result->y1 = 0;
    out_result->x2 = 0;
    out_result->y2 = 0;

    std::list<dl::detect::result_t> &detect_candidates =
        s_detector_stage1->infer(rgb565_buf, {height, width, 3});

    std::list<dl::detect::result_t> &detect_results =
        s_detector_stage2->infer(rgb565_buf, {height, width, 3}, detect_candidates);

    out_result->face_count = (int)detect_results.size();

    if (!detect_results.empty()) {
        auto &first = detect_results.front();

        if (first.box.size() >= 4) {
            out_result->has_face = true;
            out_result->x1 = first.box[0];
            out_result->y1 = first.box[1];
            out_result->x2 = first.box[2];
            out_result->y2 = first.box[3];
        }
    }

    if (out_result->has_face) {
        ESP_LOGI(TAG, "Face detected, count=%d, box=(%d,%d)-(%d,%d)",
                 out_result->face_count,
                 out_result->x1, out_result->y1,
                 out_result->x2, out_result->y2);
    } else {
        ESP_LOGI(TAG, "Face not detected");
    }

    return true;
}

void app_face_draw_result_rgb565(uint16_t *rgb565_buf, int width, int height, const app_face_result_t *result)
{
    if (rgb565_buf == nullptr || result == nullptr) {
        return;
    }

    if (!result->has_face) {
        return;
    }

    app_face_draw_rect_rgb565(rgb565_buf,
                              width,
                              height,
                              result->x1,
                              result->y1,
                              result->x2,
                              result->y2,
                              APP_FACE_BOX_COLOR_RGB565,
                              APP_FACE_BOX_THICKNESS);
}

void app_face_draw_timestamp_rgb565(uint16_t *rgb565_buf, int width, int height, const char *timestamp)
{
    if (rgb565_buf == nullptr || timestamp == nullptr) {
        return;
    }

    const int scale = 2;
    const int char_w = 5 * scale;
    const int char_h = 7 * scale;
    const int char_gap = scale;
    const int margin = 4;

    int text_len = (int)strlen(timestamp);
    if (text_len <= 0) {
        return;
    }

    int text_w = text_len * (char_w + char_gap) - char_gap;
    int text_h = char_h;

    int box_x = margin;
    int box_y = height - text_h - margin * 2;
    int box_w = text_w + margin * 2;
    int box_h = text_h + margin * 2;

    if (box_y < 0) {
        box_y = 0;
    }

    app_face_fill_rect_rgb565(rgb565_buf, width, height,
                              box_x, box_y, box_w, box_h,
                              APP_FACE_BG_COLOR_RGB565);

    app_face_draw_string_5x7_rgb565(rgb565_buf, width, height,
                                    box_x + margin,
                                    box_y + margin,
                                    timestamp,
                                    APP_FACE_TEXT_COLOR_RGB565,
                                    scale);
}