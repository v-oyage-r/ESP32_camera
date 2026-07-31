#ifndef __APP_WIFI_H
#define __APP_WIFI_H

#include "esp_err.h"   // esp_err_t, ESP_OK 等错误码类型
#include <stdbool.h>   // bool
#include <stdint.h>    // uint32_t

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    char ssid[32];
    char password[64];
    int max_retry;   // 最大重连次数，<=0 时内部会给默认值
} app_wifi_config_t;

/**
 * @brief 启动 Wi-Fi STA
 *
 * @param cfg Wi-Fi 配置
 * @return
 *      - ESP_OK: 成功启动
 *      - ESP_ERR_INVALID_ARG: 参数非法
 *      - 其他: ESP-IDF 底层错误
 */
esp_err_t app_wifi_start(const app_wifi_config_t *cfg);

/**
 * @brief 等待 Wi-Fi 连接成功
 *
 * @param timeout_ms 超时时间，单位 ms
 *                   传 0 表示一直等待
 * @return
 *      - ESP_OK: 已连接成功
 *      - ESP_FAIL: 连接失败（达到最大重试次数）
 *      - ESP_ERR_TIMEOUT: 等待超时
 *      - ESP_ERR_INVALID_STATE: 模块尚未启动
 */
esp_err_t app_wifi_wait_connected(uint32_t timeout_ms);

/**
 * @brief 查询当前是否已连接
 *
 * @return true 已连接
 * @return false 未连接
 */
bool app_wifi_is_connected(void);

/**
 * @brief 获取当前 IP 字符串
 *
 * @return const char* 当前 IP 字符串，未连接时返回空字符串 ""
 */
const char *app_wifi_get_ip_str(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_WIFI_H */