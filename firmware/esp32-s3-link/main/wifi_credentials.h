#pragma once

#include <stdbool.h>

#include "config_protocol.h"
#include "esp_err.h"

esp_err_t wifi_credentials_load(rx3_wifi_credentials_t *credentials, bool *saved);
esp_err_t wifi_credentials_save(const rx3_wifi_credentials_t *credentials);
