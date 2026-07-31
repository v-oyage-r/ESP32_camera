// #include "app_http.h"

// #include <stdio.h>
// #include <string.h>
// #include <dirent.h>
// #include <sys/stat.h>
// #include <errno.h>

// #include "esp_log.h"
// #include "esp_http_server.h"

// #include "app_sdcard.h"

// static const char *TAG = "app_http";
// static httpd_handle_t s_httpd = NULL;

// /* 首页 HTML */
// static const char *INDEX_HTML =
//     "<!DOCTYPE html>"
//     "<html>"
//     "<head>"
//     "  <meta charset=\"UTF-8\">"
//     "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
//     "  <title>ESP32-S3 Face Camera</title>"
//     "  <link rel=\"icon\" href=\"data:,\">"
//     "</head>"
//     "<body>"
//     "  <h1>ESP32-S3 Face Camera</h1>"
//     "  <p><a href=\"/photos\">Saved Photos</a></p>"
//     "</body>"
//     "</html>";

// static bool is_jpg_file(const char *name)
// {
//     if (name == NULL) {
//         return false;
//     }

//     size_t len = strlen(name);
//     if (len < 4) {
//         return false;
//     }

//     const char *ext = &name[len - 4];
//     return (strcasecmp(ext, ".jpg") == 0);
// }

// static bool is_safe_filename(const char *name)
// {
//     if (name == NULL || name[0] == '\0') {
//         return false;
//     }

//     if (strstr(name, "..") != NULL || strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
//         return false;
//     }

//     return is_jpg_file(name);
// }

// /* 处理 GET / */
// static esp_err_t index_get_handler(httpd_req_t *req)
// {
//     httpd_resp_set_type(req, "text/html; charset=UTF-8");
//     httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
//     return ESP_OK;
// }

// /* 处理 GET /favicon.ico */
// static esp_err_t favicon_get_handler(httpd_req_t *req)
// {
//     httpd_resp_set_status(req, "204 No Content");
//     httpd_resp_send(req, NULL, 0);
//     return ESP_OK;
// }

// /* 处理 GET /photos */
// static esp_err_t photos_get_handler(httpd_req_t *req)
// {
//     DIR *dir = opendir(APP_SDCARD_MOUNT_POINT);
//     if (dir == NULL) {
//         ESP_LOGE(TAG, "open dir failed: %s, errno=%d", APP_SDCARD_MOUNT_POINT, errno);
//         httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open dir failed");
//         return ESP_FAIL;
//     }

//     httpd_resp_set_type(req, "text/html; charset=UTF-8");

//     httpd_resp_sendstr_chunk(req,
//         "<!DOCTYPE html><html><head>"
//         "<meta charset=\"UTF-8\">"
//         "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
//         "<title>Saved Photos</title>"
//         "<link rel=\"icon\" href=\"data:,\">"
//         "</head><body>");

//     httpd_resp_sendstr_chunk(req, "<h1>Saved Photos</h1>");
//     httpd_resp_sendstr_chunk(req, "<p><a href=\"/\">Back</a></p>");
//     httpd_resp_sendstr_chunk(req, "<ul>");

//     struct dirent *entry;
//     char line[256];
//     int file_count = 0;

//     while ((entry = readdir(dir)) != NULL) {
//         if (!is_jpg_file(entry->d_name)) {
//             continue;
//         }

//         snprintf(line, sizeof(line),
//                  "<li><a href=\"/photo?name=%s\">%s</a></li>",
//                  entry->d_name, entry->d_name);
//         httpd_resp_sendstr_chunk(req, line);
//         file_count++;
//     }

//     closedir(dir);

//     if (file_count == 0) {
//         httpd_resp_sendstr_chunk(req, "<li>No jpg files found.</li>");
//     }

//     httpd_resp_sendstr_chunk(req, "</ul></body></html>");
//     httpd_resp_sendstr_chunk(req, NULL);

//     return ESP_OK;
// }

// /* 处理 GET /photo?name=xxx.jpg */
// static esp_err_t photo_get_handler(httpd_req_t *req)
// {
//     char query[128] = {0};
//     char name[64] = {0};

//     if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
//         httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query");
//         return ESP_FAIL;
//     }

//     if (httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
//         httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing name");
//         return ESP_FAIL;
//     }

//     if (!is_safe_filename(name)) {
//         httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid filename");
//         return ESP_FAIL;
//     }

//     char path[128];
//     snprintf(path, sizeof(path), "%s/%s", APP_SDCARD_MOUNT_POINT, name);

//     FILE *fp = fopen(path, "rb");
//     if (fp == NULL) {
//         ESP_LOGE(TAG, "open photo failed: %s, errno=%d", path, errno);
//         httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "file not found");
//         return ESP_FAIL;
//     }

//     httpd_resp_set_type(req, "image/jpeg");
//     httpd_resp_set_hdr(req, "Cache-Control", "no-store");

//     char buf[1024];
//     size_t read_len;
//     esp_err_t ret = ESP_OK;

//     while ((read_len = fread(buf, 1, sizeof(buf), fp)) > 0) {
//         ret = httpd_resp_send_chunk(req, buf, read_len);
//         if (ret != ESP_OK) {
//             break;
//         }
//     }

//     fclose(fp);

//     if (ret == ESP_OK) {
//         ret = httpd_resp_send_chunk(req, NULL, 0);
//     }

//     return ret;
// }

// esp_err_t app_http_start(void)
// {
//     if (s_httpd != NULL) {
//         ESP_LOGW(TAG, "http server already started");
//         return ESP_OK;
//     }

//     httpd_config_t config = HTTPD_DEFAULT_CONFIG();
//     config.server_port = 80;
//     config.max_uri_handlers = 4;
//     config.max_resp_headers = 4;
//     config.recv_wait_timeout = 10;
//     config.send_wait_timeout = 10;
//     config.stack_size = 4096;

//     esp_err_t ret = httpd_start(&s_httpd, &config);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
//         return ret;
//     }

//     httpd_uri_t index_uri = {
//         .uri      = "/",
//         .method   = HTTP_GET,
//         .handler  = index_get_handler,
//         .user_ctx = NULL
//     };
//     ret = httpd_register_uri_handler(s_httpd, &index_uri);
//     if (ret != ESP_OK) goto fail;

//     httpd_uri_t favicon_uri = {
//         .uri      = "/favicon.ico",
//         .method   = HTTP_GET,
//         .handler  = favicon_get_handler,
//         .user_ctx = NULL
//     };
//     ret = httpd_register_uri_handler(s_httpd, &favicon_uri);
//     if (ret != ESP_OK) goto fail;

//     httpd_uri_t photos_uri = {
//         .uri      = "/photos",
//         .method   = HTTP_GET,
//         .handler  = photos_get_handler,
//         .user_ctx = NULL
//     };
//     ret = httpd_register_uri_handler(s_httpd, &photos_uri);
//     if (ret != ESP_OK) goto fail;

//     httpd_uri_t photo_uri = {
//         .uri      = "/photo",
//         .method   = HTTP_GET,
//         .handler  = photo_get_handler,
//         .user_ctx = NULL
//     };
//     ret = httpd_register_uri_handler(s_httpd, &photo_uri);
//     if (ret != ESP_OK) goto fail;

//     ESP_LOGI(TAG, "http server started on port %d", config.server_port);
//     return ESP_OK;

// fail:
//     ESP_LOGE(TAG, "register uri handler failed: %s", esp_err_to_name(ret));
//     httpd_stop(s_httpd);
//     s_httpd = NULL;
//     return ret;
// }


#include "app_http.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "img_converters.h"

#include "app_camera.h"
#include "app_sdcard.h"

static const char *TAG = "app_http";
static httpd_handle_t s_httpd = NULL;

/* MJPEG 边界字符串 */
#define PART_BOUNDARY "frame"

/* MJPEG 响应头 */
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

/* 首页 HTML */
static const char *INDEX_HTML =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "  <meta charset=\"UTF-8\">"
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "  <title>ESP32-S3 Face Camera</title>"
    "  <link rel=\"icon\" href=\"data:,\">"
    "</head>"
    "<body>"
    "  <h1>ESP32-S3 Face Camera</h1>"
    "  <p><a href=\"/capture\">Capture</a></p>"
    "  <p><a href=\"/stream\">MJPEG Stream</a></p>"
    "  <p><a href=\"/photos\">Saved Photos</a></p>"
    "</body>"
    "</html>";

typedef struct
{
    httpd_req_t *req;
    size_t total_len;
    esp_err_t err;
} http_jpg_stream_t;

static bool is_jpg_file(const char *name)
{
    if (name == NULL) {
        return false;
    }

    size_t len = strlen(name);
    if (len < 4) {
        return false;
    }

    const char *ext = &name[len - 4];
    return (strcasecmp(ext, ".jpg") == 0);
}

static bool is_safe_filename(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }

    if (strstr(name, "..") != NULL || strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
        return false;
    }

    return is_jpg_file(name);
}

static size_t http_jpg_write_cb(void *arg, size_t index, const void *data, size_t len)
{
    (void)index;

    http_jpg_stream_t *stream = (http_jpg_stream_t *)arg;
    if (stream == NULL || stream->req == NULL || data == NULL || len == 0) {
        return 0;
    }

    if (stream->err != ESP_OK) {
        return 0;
    }

    stream->err = httpd_resp_send_chunk(stream->req, (const char *)data, len);
    if (stream->err == ESP_OK) {
        stream->total_len += len;
        return len;
    }

    return 0;
}

static esp_err_t send_rgb565_frame_as_jpeg(httpd_req_t *req, camera_fb_t *fb, uint8_t quality)
{
    if (req == NULL || fb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (fb->format != PIXFORMAT_RGB565) {
        ESP_LOGE(TAG, "frame format is not RGB565, format=%d", fb->format);
        return ESP_FAIL;
    }

    http_jpg_stream_t stream = {
        .req = req,
        .total_len = 0,
        .err = ESP_OK,
    };

    bool ok = fmt2jpg_cb(fb->buf,
                         fb->len,
                         fb->width,
                         fb->height,
                         fb->format,
                         quality,
                         http_jpg_write_cb,
                         &stream);

    if (!ok) {
        ESP_LOGE(TAG, "fmt2jpg_cb failed");
        return ESP_FAIL;
    }

    if (stream.err != ESP_OK) {
        ESP_LOGE(TAG, "http chunk send failed: %s", esp_err_to_name(stream.err));
        return stream.err;
    }

    return ESP_OK;
}

/* 处理 GET / */
static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* 处理 GET /favicon.ico */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* 处理 GET /capture */
static esp_err_t capture_get_handler(httpd_req_t *req)
{
    camera_fb_t *fb = app_camera_capture();
    if (fb == NULL) {
        ESP_LOGE(TAG, "camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    esp_err_t ret = send_rgb565_frame_as_jpeg(req, fb, 80);

    app_camera_capture_done(fb);

    if (ret == ESP_OK) {
        ret = httpd_resp_send_chunk(req, NULL, 0);
    } else {
        httpd_resp_send_500(req);
    }

    return ret;
}

/* 处理 GET /stream */
static esp_err_t stream_get_handler(httpd_req_t *req)
{
    char part_buf[64];
    esp_err_t ret = ESP_OK;

    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (1) {
        camera_fb_t *fb = app_camera_capture();
        if (fb == NULL) {
            ESP_LOGE(TAG, "stream capture failed");
            ret = ESP_FAIL;
            break;
        }

        if (fb->format != PIXFORMAT_RGB565) {
            ESP_LOGE(TAG, "stream frame format is not RGB565, format=%d", fb->format);
            app_camera_capture_done(fb);
            ret = ESP_FAIL;
            break;
        }

        /* 先把这一帧 JPEG 编码到内存，拿到长度后再发 multipart 头
         * 这是为了保持 Content-Length 正确
         */
        uint8_t *jpg_buf = NULL;
        size_t jpg_len = 0;

        bool ok = fmt2jpg(fb->buf,
                          fb->len,
                          fb->width,
                          fb->height,
                          fb->format,
                          80,
                          &jpg_buf,
                          &jpg_len);

        app_camera_capture_done(fb);

        if (!ok || jpg_buf == NULL || jpg_len == 0) {
            if (jpg_buf) {
                free(jpg_buf);
            }
            ESP_LOGE(TAG, "fmt2jpg failed in stream");
            ret = ESP_FAIL;
            break;
        }

        ret = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (ret != ESP_OK) {
            free(jpg_buf);
            break;
        }

        int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, (unsigned)jpg_len);
        ret = httpd_resp_send_chunk(req, part_buf, hlen);
        if (ret != ESP_OK) {
            free(jpg_buf);
            break;
        }

        ret = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_len);
        free(jpg_buf);

        if (ret != ESP_OK) {
            break;
        }

        ret = httpd_resp_send_chunk(req, "\r\n", 2);
        if (ret != ESP_OK) {
            break;
        }

        /* RGB565 -> JPEG 软件编码开销更大，稍微留一点余量 */
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_LOGW(TAG, "stream ended: %s", esp_err_to_name(ret));
    return ret;
}

/* 处理 GET /photos */
static esp_err_t photos_get_handler(httpd_req_t *req)
{
    DIR *dir = opendir(APP_SDCARD_MOUNT_POINT);
    if (dir == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open dir failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>Saved Photos</title>"
        "<link rel=\"icon\" href=\"data:,\">"
        "</head><body>");
    httpd_resp_sendstr_chunk(req, "<h1>Saved Photos</h1>");
    httpd_resp_sendstr_chunk(req, "<p><a href=\"/\">Back</a></p>");
    httpd_resp_sendstr_chunk(req, "<ul>");

    struct dirent *entry;
    char line[256];
    int file_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (!is_jpg_file(entry->d_name)) {
            continue;
        }

        snprintf(line, sizeof(line),
                 "<li><a href=\"/photo?name=%s\">%s</a></li>",
                 entry->d_name, entry->d_name);
        httpd_resp_sendstr_chunk(req, line);
        file_count++;
    }

    closedir(dir);

    if (file_count == 0) {
        httpd_resp_sendstr_chunk(req, "<li>No jpg files found.</li>");
    }

    httpd_resp_sendstr_chunk(req, "</ul></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

/* 处理 GET /photo?name=xxx.jpg */
static esp_err_t photo_get_handler(httpd_req_t *req)
{
    char query[128] = {0};
    char name[64] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing name");
        return ESP_FAIL;
    }

    if (!is_safe_filename(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid filename");
        return ESP_FAIL;
    }

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", APP_SDCARD_MOUNT_POINT, name);

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "open photo failed: %s, errno=%d", path, errno);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "file not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char buf[1024];
    size_t read_len;
    esp_err_t ret = ESP_OK;

    while ((read_len = fread(buf, 1, sizeof(buf), fp)) > 0) {
        ret = httpd_resp_send_chunk(req, buf, read_len);
        if (ret != ESP_OK) {
            break;
        }
    }

    fclose(fp);

    if (ret == ESP_OK) {
        ret = httpd_resp_send_chunk(req, NULL, 0);
    }

    return ret;
}

esp_err_t app_http_start(void)
{
    if (s_httpd != NULL) {
        ESP_LOGW(TAG, "http server already started");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 10;
    config.max_resp_headers = 8;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    config.stack_size = 6144;

    esp_err_t ret = httpd_start(&s_httpd, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_uri_t index_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_get_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(s_httpd, &index_uri);
    if (ret != ESP_OK) goto fail;

    httpd_uri_t favicon_uri = {
        .uri      = "/favicon.ico",
        .method   = HTTP_GET,
        .handler  = favicon_get_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(s_httpd, &favicon_uri);
    if (ret != ESP_OK) goto fail;

    httpd_uri_t capture_uri = {
        .uri      = "/capture",
        .method   = HTTP_GET,
        .handler  = capture_get_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(s_httpd, &capture_uri);
    if (ret != ESP_OK) goto fail;

    httpd_uri_t stream_uri = {
        .uri      = "/stream",
        .method   = HTTP_GET,
        .handler  = stream_get_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(s_httpd, &stream_uri);
    if (ret != ESP_OK) goto fail;

    httpd_uri_t photos_uri = {
        .uri      = "/photos",
        .method   = HTTP_GET,
        .handler  = photos_get_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(s_httpd, &photos_uri);
    if (ret != ESP_OK) goto fail;

    httpd_uri_t photo_uri = {
        .uri      = "/photo",
        .method   = HTTP_GET,
        .handler  = photo_get_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(s_httpd, &photo_uri);
    if (ret != ESP_OK) goto fail;

    ESP_LOGI(TAG, "http server started on port %d", config.server_port);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "register uri handler failed: %s", esp_err_to_name(ret));
    httpd_stop(s_httpd);
    s_httpd = NULL;
    return ret;
}