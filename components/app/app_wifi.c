#include "app_wifi.h"

#include <stdio.h>      // snprintf
#include <string.h>     // memset, strncpy

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#define WIFI_CONNECTED_BIT    BIT0
#define WIFI_FAIL_BIT         BIT1

static const char *TAG = "app_wifi";

/* Wi-Fi 事件组 */
static EventGroupHandle_t s_wifi_event_group = NULL;

/* 连接状态 */
static int s_retry_num = 0;
static int s_max_retry = 5;
static bool s_connected = false;
static char s_ip_str[16] = {0};

/* 标记网络栈/事件循环是否已经初始化过 */
static bool s_netif_inited = false;
static bool s_event_loop_inited = false;
static bool s_wifi_inited = false;

/* 事件处理实例，用于后续如需注销时使用 */
static esp_event_handler_instance_t s_wifi_any_id_instance = NULL;
static esp_event_handler_instance_t s_ip_got_ip_instance = NULL;

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "wifi sta start, connecting to ap...");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        s_connected = false;
        s_ip_str[0] = '\0';

        /* 清除连接位，避免状态残留 */
        if (s_wifi_event_group != NULL)
        {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }

        if (s_retry_num < s_max_retry)
        {
            esp_err_t err = esp_wifi_connect();
            if (err == ESP_OK)
            {
                s_retry_num++;
                ESP_LOGW(TAG, "wifi disconnected, retry %d/%d",
                         s_retry_num, s_max_retry);
            }
            else
            {
                ESP_LOGE(TAG, "esp_wifi_connect failed during retry: %s",
                         esp_err_to_name(err));
                if (s_wifi_event_group != NULL)
                {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                }
            }
        }
        else
        {
            ESP_LOGE(TAG, "connect to ap failed after max retries");
            if (s_wifi_event_group != NULL)
            {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        s_retry_num = 0;
        s_connected = true;

        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));

        if (s_wifi_event_group != NULL)
        {
            xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }

        ESP_LOGI(TAG, "got ip: %s", s_ip_str);
    }
}

esp_err_t app_wifi_start(const app_wifi_config_t *cfg)
{
    esp_err_t ret;

    if (cfg == NULL)
    {
        ESP_LOGE(TAG, "cfg is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (cfg->ssid[0] == '\0')
    {
        ESP_LOGE(TAG, "ssid is empty");
        return ESP_ERR_INVALID_ARG;
    }

    /* 创建事件组 */
    if (s_wifi_event_group == NULL)
    {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL)
        {
            ESP_LOGE(TAG, "failed to create wifi event group");
            return ESP_ERR_NO_MEM;
        }
    }

    /* 重置运行时状态 */
    s_retry_num = 0;
    s_max_retry = (cfg->max_retry > 0) ? cfg->max_retry : 5;
    s_connected = false;
    s_ip_str[0] = '\0';

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    /* 初始化网络栈 */
    if (!s_netif_inited)
    {
        ret = esp_netif_init();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        s_netif_inited = true;
    }

    /* 创建默认事件循环
       如果外部已创建，这里会返回 ESP_ERR_INVALID_STATE，可视为可接受 */
    if (!s_event_loop_inited)
    {
        ret = esp_event_loop_create_default();
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE)
        {
            s_event_loop_inited = true;
        }
        else
        {
            ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s",
                     esp_err_to_name(ret));
            return ret;
        }
    }

    /* 创建默认 STA netif */
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == NULL)
    {
        sta_netif = esp_netif_create_default_wifi_sta();
        if (sta_netif == NULL)
        {
            ESP_LOGE(TAG, "failed to create default wifi sta netif");
            return ESP_FAIL;
        }
    }

    /* 初始化 Wi-Fi 驱动 */
    if (!s_wifi_inited)
    {
        wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();

        ret = esp_wifi_init(&wifi_init_cfg);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        s_wifi_inited = true;
    }

    /* 注册事件处理 */
    if (s_wifi_any_id_instance == NULL)
    {
        ret = esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL,
            &s_wifi_any_id_instance
        );
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "register WIFI_EVENT handler failed: %s",
                     esp_err_to_name(ret));
            return ret;
        }
    }

    if (s_ip_got_ip_instance == NULL)
    {
        ret = esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL,
            &s_ip_got_ip_instance
        );
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "register IP_EVENT handler failed: %s",
                     esp_err_to_name(ret));
            return ret;
        }
    }

    /* 配置 Wi-Fi */
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));

    strncpy((char *)wifi_config.sta.ssid,
            cfg->ssid,
            sizeof(wifi_config.sta.ssid) - 1);

    strncpy((char *)wifi_config.sta.password,
            cfg->password,
            sizeof(wifi_config.sta.password) - 1);

    /* 对你的手机热点来说，这样更稳一点 */
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    /* 可选：PMF 配置，ESP-IDF 常见默认写法 */
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_CONN)
    {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "wifi start ok, ssid=%s", cfg->ssid);
    return ESP_OK;
}

esp_err_t app_wifi_wait_connected(uint32_t timeout_ms)
{
    if (s_wifi_event_group == NULL)
    {
        ESP_LOGE(TAG, "wifi module not started");
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t wait_ticks = (timeout_ms == 0) ?
                            portMAX_DELAY :
                            pdMS_TO_TICKS(timeout_ms);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        wait_ticks
    );

    if (bits & WIFI_CONNECTED_BIT)
    {
        return ESP_OK;
    }

    if (bits & WIFI_FAIL_BIT)
    {
        return ESP_FAIL;
    }

    return ESP_ERR_TIMEOUT;
}

bool app_wifi_is_connected(void)
{
    return s_connected;
}

const char *app_wifi_get_ip_str(void)
{
    return s_ip_str;
}