#ifndef __APP_RUNTIME_H
#define __APP_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 系统运行模式
 */
typedef enum
{
    APP_MODE_IDLE = 0,        /* 空闲待命 */
    APP_MODE_CAPTURE          /* 定时抓拍检测 */
} app_runtime_mode_t;

/**
 * @brief 运行时状态快照
 *
 * 说明：
 * 1. 这是给 UI / 调试 / 业务层读取的只读快照结构体
 * 2. 里面尽量只放“状态”，不要放大块数据
 */
typedef struct
{
    bool wifi_ready;          /* Wi-Fi 是否已连接 */
    bool sd_ready;            /* TF 卡是否已挂载 */
    bool http_ready;          /* HTTP 服务是否已启动 */
    bool time_synced;         /* 是否已完成对时 */
    bool face_ready;          /* 人脸检测器是否已初始化 */

    app_runtime_mode_t mode;  /* 当前运行模式 */

    char ip_str[32];          /* 当前 IP */
    char time_str[32];        /* 当前时间字符串 */
    char last_result[64];     /* 最近一次操作结果 */
} app_runtime_status_t;

/**
 * @brief 启动运行时系统
 *
 * 主要职责：
 * - 完成各模块初始化
 * - 创建内部任务
 * - 进入运行态
 *
 * @return ESP_OK 启动成功
 * @return 其他值 启动失败
 */
esp_err_t app_runtime_start(void);

/**
 * @brief 获取当前运行时状态快照
 *
 * @param out_status 输出状态结构体
 * @return ESP_OK 成功
 * @return ESP_ERR_INVALID_ARG 参数无效
 */
esp_err_t app_runtime_get_status(app_runtime_status_t *out_status);

/**
 * @brief 获取当前运行模式
 *
 * @return 当前模式
 */
app_runtime_mode_t app_runtime_get_mode(void);

/**
 * @brief 设置运行模式
 *
 * 说明：
 * - 外部可以直接切换模式
 * - 后续 KEY0 也会内部调用这套接口
 *
 * @param mode 目标模式
 * @return ESP_OK 成功
 */
esp_err_t app_runtime_set_mode(app_runtime_mode_t mode);

/**
 * @brief 切换抓拍模式
 *
 * 逻辑：
 * - 当前是 IDLE -> 切到 CAPTURE
 * - 当前是 CAPTURE -> 切到 IDLE
 *
 * @return 切换后的模式
 */
app_runtime_mode_t app_runtime_toggle_capture_mode(void);

/**
 * @brief 触发一次立即抓拍
 *
 * 说明：
 * - 这个接口主要给调试或手动触发使用
 * - 具体是否真的执行，由内部状态决定
 *
 * @return ESP_OK 触发成功
 */
esp_err_t app_runtime_trigger_capture_once(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_RUNTIME_H */