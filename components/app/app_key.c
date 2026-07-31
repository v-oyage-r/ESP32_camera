#include "app_key.h"

#include "xl9555.h"

/*
 * 说明：
 * 1. 直接复用 BSP 里的 XL9555 按键定义：
 *      KEY0 / KEY1 / KEY2 / KEY3
 *      KEY0_PRES ~ KEY3_PRES
 *      xl9555_key_scan()
 * 2. 这样最贴合你当前板级驱动，不再自己猜 IO1_7 之类的命名。
 */

void app_key_init(void)
{
    /*
     * 当前按键由 XL9555 统一管理，xl9555_init() 完成后即可直接扫描。
     * 这里暂时无需额外初始化，保留接口即可。
     */
}

uint8_t app_key_scan(uint8_t mode)
{
    uint8_t key = xl9555_key_scan(mode);

    switch (key)
    {
        case KEY0_PRES:
            return APP_KEY0_PRES;

        case KEY1_PRES:
            return APP_KEY1_PRES;

        case KEY2_PRES:
            return APP_KEY2_PRES;

        case KEY3_PRES:
            return APP_KEY3_PRES;

        default:
            return APP_KEY_NONE;
    }
}

bool app_key_is_pressed(app_key_id_t key_id)
{
    switch (key_id)
    {
        case APP_KEY_ID_0:
            return (KEY0 == 0);

        case APP_KEY_ID_1:
            return (KEY1 == 0);

        case APP_KEY_ID_2:
            return (KEY2 == 0);

        case APP_KEY_ID_3:
            return (KEY3 == 0);

        default:
            return false;
    }
}