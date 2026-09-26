#include "wifi_credentials.h"

#include <string.h>

#include "nvs.h"
#include "sdkconfig.h"

#define WIFI_CREDENTIALS_NAMESPACE "rx3-link"
#define WIFI_CREDENTIALS_KEY "wifi-config"
#define WIFI_CREDENTIALS_VERSION 1

typedef struct {
    uint8_t version;
    uint8_t ssid_length;
    uint8_t password_length;
    char ssid[RX3_CONFIG_SSID_MAX_LENGTH];
    char password[RX3_CONFIG_PASSWORD_MAX_LENGTH];
} stored_wifi_credentials_t;

static bool stored_credentials_valid(const stored_wifi_credentials_t *stored)
{
    return stored->version == WIFI_CREDENTIALS_VERSION &&
           stored->ssid_length > 0 &&
           stored->ssid_length <= RX3_CONFIG_SSID_MAX_LENGTH &&
           stored->password_length <= RX3_CONFIG_PASSWORD_MAX_LENGTH &&
           (stored->password_length == 0 || stored->password_length >= 8);
}

static void copy_stored_credentials(rx3_wifi_credentials_t *credentials,
                                    const stored_wifi_credentials_t *stored)
{
    memset(credentials, 0, sizeof(*credentials));
    credentials->ssid_length = stored->ssid_length;
    credentials->password_length = stored->password_length;
    memcpy(credentials->ssid, stored->ssid, stored->ssid_length);
    memcpy(credentials->password, stored->password, stored->password_length);
}

static esp_err_t load_saved_credentials(rx3_wifi_credentials_t *credentials)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READONLY, &handle);
    if (result != ESP_OK) {
        return result;
    }

    stored_wifi_credentials_t stored;
    size_t stored_size = sizeof(stored);
    result = nvs_get_blob(handle, WIFI_CREDENTIALS_KEY, &stored, &stored_size);
    nvs_close(handle);
    if (result != ESP_OK) {
        memset(&stored, 0, sizeof(stored));
        return result;
    }
    if (stored_size != sizeof(stored) || !stored_credentials_valid(&stored)) {
        memset(&stored, 0, sizeof(stored));
        return ESP_ERR_INVALID_STATE;
    }

    copy_stored_credentials(credentials, &stored);
    memset(&stored, 0, sizeof(stored));
    return ESP_OK;
}

static esp_err_t load_compile_time_credentials(rx3_wifi_credentials_t *credentials)
{
    const size_t ssid_length = strlen(CONFIG_RX3_LINK_WIFI_SSID);
    const size_t password_length = strlen(CONFIG_RX3_LINK_WIFI_PASSWORD);
    if (ssid_length == 0 || ssid_length > RX3_CONFIG_SSID_MAX_LENGTH ||
        password_length > RX3_CONFIG_PASSWORD_MAX_LENGTH ||
        (password_length > 0 && password_length < 8)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(credentials, 0, sizeof(*credentials));
    credentials->ssid_length = ssid_length;
    credentials->password_length = password_length;
    memcpy(credentials->ssid, CONFIG_RX3_LINK_WIFI_SSID, ssid_length);
    memcpy(credentials->password, CONFIG_RX3_LINK_WIFI_PASSWORD, password_length);
    return ESP_OK;
}

esp_err_t wifi_credentials_load(rx3_wifi_credentials_t *credentials, bool *saved)
{
    if (credentials == NULL || saved == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result = load_saved_credentials(credentials);
    if (result == ESP_OK) {
        *saved = true;
        return ESP_OK;
    }
    if (result != ESP_ERR_NVS_NOT_FOUND && result != ESP_ERR_NVS_NOT_INITIALIZED &&
        result != ESP_ERR_INVALID_STATE) {
        return result;
    }

    *saved = false;
    return load_compile_time_credentials(credentials);
}

esp_err_t wifi_credentials_save(const rx3_wifi_credentials_t *credentials)
{
    if (credentials == NULL || credentials->ssid_length == 0 ||
        credentials->ssid_length > RX3_CONFIG_SSID_MAX_LENGTH ||
        credentials->password_length > RX3_CONFIG_PASSWORD_MAX_LENGTH ||
        (credentials->password_length > 0 && credentials->password_length < 8)) {
        return ESP_ERR_INVALID_ARG;
    }

    stored_wifi_credentials_t stored = {
        .version = WIFI_CREDENTIALS_VERSION,
        .ssid_length = credentials->ssid_length,
        .password_length = credentials->password_length,
    };
    memcpy(stored.ssid, credentials->ssid, credentials->ssid_length);
    memcpy(stored.password, credentials->password, credentials->password_length);

    nvs_handle_t handle;
    esp_err_t result = nvs_open(WIFI_CREDENTIALS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        memset(&stored, 0, sizeof(stored));
        return result;
    }

    result = nvs_set_blob(handle, WIFI_CREDENTIALS_KEY, &stored, sizeof(stored));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    memset(&stored, 0, sizeof(stored));
    return result;
}
