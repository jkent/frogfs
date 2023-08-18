//#include "protocol_examples_common.h"
//#include "file_serving_example_common.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "frogfs/frogfs.h"
#include "frogfs/vfs.h"

extern const uint8_t frogfs_bin[];
TaskHandle_t main_task;

extern esp_err_t start_http_server(const char *base_path);

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    static int retry_num = 0;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
            event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_num < 5) {
            esp_wifi_connect();
            retry_num++;
        } else {
            esp_restart();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        retry_num = 0;
        xTaskNotify(main_task, 0, eNoAction);
    }
}

esp_err_t wifi_init_sta(void)
{
    esp_err_t err;

    err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        return err;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              &event_handler, NULL,
                                              &instance_any_id);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              &event_handler, NULL,
                                              &instance_got_ip);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);

    return ESP_OK;
}

void app_main(void)
{
    main_task = xTaskGetCurrentTaskHandle();

    // Mount frogfs
    frogfs_config_t frogfs_config = {
        .addr = frogfs_bin,
    };
    frogfs_fs_t *frogfs = frogfs_init(&frogfs_config);
    assert(frogfs != NULL);

    frogfs_vfs_conf_t frogfs_vfs_conf = {
        .base_path = "/files",
        .fs = frogfs,
        .max_files = 5,
    };
    frogfs_vfs_register(&frogfs_vfs_conf);

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
            ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize wifi in STA mode
    ESP_ERROR_CHECK(wifi_init_sta());

    // Start http server
    ESP_ERROR_CHECK(start_http_server("/files"));
}
