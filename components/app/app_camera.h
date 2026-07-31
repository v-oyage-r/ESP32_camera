#ifndef __APP_CAMERA_H
#define __APP_CAMERA_H

#include "esp_err.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_camera_init(void);
camera_fb_t *app_camera_capture(void);
void app_camera_capture_done(camera_fb_t *fb);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CAMERA_H */