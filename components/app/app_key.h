#ifndef __APP_KEY_H
#define __APP_KEY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 统一应用层按键返回值 */
#define APP_KEY_NONE      0
#define APP_KEY0_PRES     1
#define APP_KEY1_PRES     2
#define APP_KEY2_PRES     3
#define APP_KEY3_PRES     4

typedef enum
{
    APP_KEY_ID_0 = 0,
    APP_KEY_ID_1,
    APP_KEY_ID_2,
    APP_KEY_ID_3,
    APP_KEY_ID_MAX
} app_key_id_t;

/**
 * @brief 初始化按键模块
 *
 * 当前按键由 XL9555 管理，通常在 xl9555_init() 完成后即可直接使用。
 * 这里保留接口，方便后续扩展。
 */
void app_key_init(void);

/**
 * @brief 扫描按键
 *
 * @param mode
 *      0: 不支持连续按（按住不放只返回一次）
 *      1: 支持连续按（按住不放每次扫描都可能返回）
 *
 * @return
 *      APP_KEY_NONE / APP_KEY0_PRES / APP_KEY1_PRES / APP_KEY2_PRES / APP_KEY3_PRES
 */
uint8_t app_key_scan(uint8_t mode);

/**
 * @brief 读取某个按键当前是否按下（原始状态）
 *
 * @param key_id APP_KEY_ID_0 ~ APP_KEY_ID_3
 * @return true  按下
 * @return false 未按下
 */
bool app_key_is_pressed(app_key_id_t key_id);

#ifdef __cplusplus
}
#endif

#endif /* __APP_KEY_H */