#include <stdio.h>

#include "esp_err.h"

esp_err_t httpd_start(void);
esp_err_t storage_init(void);
esp_err_t network_init(void);

void app_main(void)
{
    ESP_ERROR_CHECK(storage_init());
    ESP_ERROR_CHECK(network_init());
    ESP_ERROR_CHECK(httpd_start());
}
