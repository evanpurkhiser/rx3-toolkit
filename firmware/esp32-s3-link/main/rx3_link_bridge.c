#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bridge_config.h"
#include "config_protocol.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_private/wifi.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp32s3/rom/usb/chip_usb_dw_wrapper.h"
#include "esp32s3/rom/usb/usb_persist.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_credentials.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tinyusb_net.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "tusb.h"
#include "wear_levelling.h"

static const char *TAG = "rx3-link";

typedef struct {
    uint64_t usb_rx_frames;
    uint64_t usb_rx_bytes;
    uint64_t usb_rx_drops;
    uint64_t wifi_rx_frames;
    uint64_t wifi_rx_bytes;
    uint64_t wifi_rx_drops;
    uint64_t wifi_reconnects;
    uint64_t usb_link_changes;
    uint64_t wifi_tx_successes;
    uint64_t wifi_tx_failures;
    int32_t last_wifi_tx_result;
} bridge_counters_t;

typedef struct {
    portMUX_TYPE lock;
    bridge_counters_t counters;
    bool wifi_connected;
    bool usb_link_up;
} bridge_state_t;

static bridge_state_t s_bridge = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
};

static wl_handle_t s_msc_wl_handle = WL_INVALID_HANDLE;
static tinyusb_msc_storage_handle_t s_msc_storage;
static uint8_t s_bridge_mac[BRIDGE_MAC_LENGTH];
static TaskHandle_t s_bootloader_task;
static QueueHandle_t s_config_commands;
static rx3_wifi_credentials_t s_wifi_credentials;
static bool s_wifi_credentials_saved;

#define BRIDGE_MAX_FRAME_SIZE 1600
#define ETHERNET_MIN_FRAME_SIZE 60
#define BRIDGE_DIAGNOSTIC_ETHERTYPE 0x88b5
#define BRIDGE_DIAGNOSTIC_REQUEST "RX3STAT?"
#define BRIDGE_DIAGNOSTIC_RESPONSE "RX3STAT1"
#define BRIDGE_BOOTLOADER_REQUEST "RX3BOOT?"
#define BRIDGE_BOOTLOADER_RESPONSE "RX3BOOT1"
#define ETHERNET_HEADER_SIZE 14

typedef struct {
    uint8_t ethernet_addresses[12];
    uint8_t opcode;
    uint16_t request_id;
    rx3_wifi_credentials_t credentials;
} config_command_t;

enum {
    RX3_BOOT_ITF_NCM_CONTROL = 0,
    RX3_BOOT_ITF_NCM_DATA,
    RX3_BOOT_ITF_MSC,
    RX3_BOOT_ITF_COUNT,
};

enum {
    RX3_BOOT_EP_NCM_NOTIFICATION = 1,
    RX3_BOOT_EP_NCM_DATA,
    RX3_BOOT_EP_MSC,
};

enum {
    RX3_STR_MSC = 4,
    RX3_STR_NCM,
    RX3_STR_MAC,
};

#define RX3_BOOT_USB_CONFIG_LENGTH \
    (TUD_CONFIG_DESC_LEN + TUD_CDC_NCM_DESC_LEN + TUD_MSC_DESC_LEN)

/*
 * Firmware 1.19's CONFIG_PDJ USB core stops registering interfaces as soon as
 * it registers a mass-storage interface. NCM must therefore precede MSC even
 * though Espressif's generated composite descriptor uses the opposite order.
 */
static const uint8_t s_rx3_boot_usb_config[] = {
    TUD_CONFIG_DESCRIPTOR(1,
                          RX3_BOOT_ITF_COUNT,
                          0,
                          RX3_BOOT_USB_CONFIG_LENGTH,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
                          100),
    TUD_CDC_NCM_DESCRIPTOR(RX3_BOOT_ITF_NCM_CONTROL,
                           RX3_STR_NCM,
                           RX3_STR_MAC,
                           0x80 | RX3_BOOT_EP_NCM_NOTIFICATION,
                           64,
                           RX3_BOOT_EP_NCM_DATA,
                           0x80 | RX3_BOOT_EP_NCM_DATA,
                           64,
                           CFG_TUD_NET_MTU),
    TUD_MSC_DESCRIPTOR(RX3_BOOT_ITF_MSC,
                       RX3_STR_MSC,
                       RX3_BOOT_EP_MSC,
                       0x80 | RX3_BOOT_EP_MSC,
                       64),
};

typedef struct {
    uint16_t length;
    uint8_t data[BRIDGE_MAX_FRAME_SIZE];
} bridge_frame_t;

static bridge_frame_t s_wifi_to_usb_frames[CONFIG_RX3_LINK_WIFI_TO_USB_QUEUE_DEPTH];
static QueueHandle_t s_wifi_to_usb_free;
static QueueHandle_t s_wifi_to_usb_pending;

static bool is_control_request(const uint8_t *frame,
                               uint16_t length,
                               const char *request,
                               size_t request_length)
{
    return length >= ETHERNET_HEADER_SIZE + request_length &&
           frame[12] == (BRIDGE_DIAGNOSTIC_ETHERTYPE >> 8) &&
           frame[13] == (BRIDGE_DIAGNOSTIC_ETHERTYPE & 0xff) &&
           memcmp(frame + ETHERNET_HEADER_SIZE, request, request_length) == 0;
}

static void queue_control_response(const uint8_t *request, const char *payload)
{
    bridge_frame_t *response = NULL;
    if (xQueueReceive(s_wifi_to_usb_free, &response, 0) != pdTRUE) {
        return;
    }

    memcpy(response->data, request + 6, 6);
    memcpy(response->data + 6, request, 6);
    response->data[12] = BRIDGE_DIAGNOSTIC_ETHERTYPE >> 8;
    response->data[13] = BRIDGE_DIAGNOSTIC_ETHERTYPE & 0xff;

    const int payload_length = snprintf((char *)response->data + ETHERNET_HEADER_SIZE,
                                        BRIDGE_MAX_FRAME_SIZE - ETHERNET_HEADER_SIZE,
                                        "%s",
                                        payload);

    if (payload_length < 0 ||
        payload_length >= BRIDGE_MAX_FRAME_SIZE - ETHERNET_HEADER_SIZE) {
        xQueueSend(s_wifi_to_usb_free, &response, 0);
        return;
    }

    response->length = ETHERNET_HEADER_SIZE + payload_length;
    if (xQueueSend(s_wifi_to_usb_pending, &response, 0) != pdTRUE) {
        xQueueSend(s_wifi_to_usb_free, &response, 0);
    }
}

static void queue_config_response(const uint8_t *ethernet_addresses,
                                  const rx3_config_request_t *request,
                                  rx3_s3_config_status_code_t status,
                                  const uint8_t *payload,
                                  uint16_t payload_length)
{
    bridge_frame_t *response = NULL;
    if (xQueueReceive(s_wifi_to_usb_free, &response, 0) != pdTRUE) {
        return;
    }

    memcpy(response->data, ethernet_addresses + 6, 6);
    memcpy(response->data + 6, ethernet_addresses, 6);
    response->data[12] = RX3_S3_CONFIG_ETHERTYPE >> 8;
    response->data[13] = RX3_S3_CONFIG_ETHERTYPE & 0xff;
    const size_t response_length = rx3_config_write_response(
        response->data + ETHERNET_HEADER_SIZE,
        BRIDGE_MAX_FRAME_SIZE - ETHERNET_HEADER_SIZE,
        request,
        status,
        payload,
        payload_length);
    if (response_length == 0) {
        xQueueSend(s_wifi_to_usb_free, &response, 0);
        return;
    }

    response->length = ETHERNET_HEADER_SIZE + response_length;
    if (xQueueSend(s_wifi_to_usb_pending, &response, 0) != pdTRUE) {
        xQueueSend(s_wifi_to_usb_free, &response, 0);
    }
}

static void queue_live_status_response(const uint8_t *ethernet_addresses,
                                       const rx3_config_request_t *request)
{
    rx3_config_live_status_t status = {
        .rssi = -127,
    };
    bool wifi_connected;
    bool usb_link_up;

    portENTER_CRITICAL(&s_bridge.lock);
    wifi_connected = s_bridge.wifi_connected;
    usb_link_up = s_bridge.usb_link_up;
    status.ssid_length = s_wifi_credentials.ssid_length;
    memcpy(status.ssid, s_wifi_credentials.ssid, s_wifi_credentials.ssid_length);
    if (s_wifi_credentials.ssid_length != 0) {
        status.flags |= RX3_S3_CONFIG_FLAG_CREDENTIALS_CONFIGURED;
    }
    if (s_wifi_credentials.password_length != 0) {
        status.flags |= RX3_S3_CONFIG_FLAG_PASSWORD_SET;
    }
    if (s_wifi_credentials_saved) {
        status.flags |= RX3_S3_CONFIG_FLAG_SAVED_OVERRIDE;
    }
    portEXIT_CRITICAL(&s_bridge.lock);

    if (wifi_connected) {
        status.flags |= RX3_S3_CONFIG_FLAG_WIFI_CONNECTED;
    }
    if (usb_link_up) {
        status.flags |= RX3_S3_CONFIG_FLAG_NCM_LINK_UP;
    }
    memcpy(status.mac, s_bridge_mac, sizeof(status.mac));

    wifi_ap_record_t access_point;
    if (wifi_connected && esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) {
        status.rssi = access_point.rssi;
    }

    uint8_t payload[RX3_CONFIG_STATUS_PAYLOAD_SIZE];
    const size_t payload_length = rx3_config_write_status_payload(payload,
                                                                   sizeof(payload),
                                                                   &status);
    if (payload_length == 0) {
        queue_config_response(ethernet_addresses,
                              request,
                              RX3_S3_CONFIG_INTERNAL_ERROR,
                              NULL,
                              0);
        return;
    }
    queue_config_response(ethernet_addresses,
                          request,
                          RX3_S3_CONFIG_OK,
                          payload,
                          payload_length);
}

static void queue_status_response(const uint8_t *request)
{
    bridge_counters_t counters;
    bool wifi_connected;
    bool usb_link_up;

    portENTER_CRITICAL(&s_bridge.lock);
    counters = s_bridge.counters;
    wifi_connected = s_bridge.wifi_connected;
    usb_link_up = s_bridge.usb_link_up;
    portEXIT_CRITICAL(&s_bridge.lock);

    wifi_ap_record_t access_point;
    const int rssi = wifi_connected && esp_wifi_sta_get_ap_info(&access_point) == ESP_OK
                         ? access_point.rssi
                         : -127;

    char status[256];
    snprintf(status,
             sizeof(status),
             BRIDGE_DIAGNOSTIC_RESPONSE
             " wifi=%u ncm=%u last_tx=%" PRId32
             " usb_rx=%" PRIu64 " usb_drop=%" PRIu64
             " wifi_rx=%" PRIu64 " wifi_drop=%" PRIu64
             " tx_ok=%" PRIu64 " tx_fail=%" PRIu64 " rssi=%d",
             wifi_connected,
             usb_link_up,
             counters.last_wifi_tx_result,
             counters.usb_rx_frames,
             counters.usb_rx_drops,
             counters.wifi_rx_frames,
             counters.wifi_rx_drops,
             counters.wifi_tx_successes,
             counters.wifi_tx_failures,
             rssi);
    queue_control_response(request, status);
}

static esp_err_t apply_wifi_credentials(const rx3_wifi_credentials_t *credentials)
{
    wifi_config_t station = {
        .sta = {
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
        },
    };
    memcpy(station.sta.ssid, credentials->ssid, credentials->ssid_length);
    memcpy(station.sta.password, credentials->password, credentials->password_length);
    const esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &station);
    memset(&station, 0, sizeof(station));
    return result;
}

static void request_wifi_reconnect(void)
{
    const esp_err_t disconnect_result = esp_wifi_disconnect();
    if (disconnect_result == ESP_OK) {
        return;
    }
    if (disconnect_result != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGW(TAG, "Wi-Fi disconnect request failed: %s", esp_err_to_name(disconnect_result));
        return;
    }

    const esp_err_t connect_result = esp_wifi_connect();
    if (connect_result != ESP_OK && connect_result != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "Wi-Fi connect request failed: %s", esp_err_to_name(connect_result));
    }
}

static void config_command_task(void *argument)
{
    (void)argument;

    while (true) {
        config_command_t command;
        xQueueReceive(s_config_commands, &command, portMAX_DELAY);
        const rx3_config_request_t request = {
            .opcode = command.opcode,
            .request_id = command.request_id,
        };
        rx3_s3_config_status_code_t status = RX3_S3_CONFIG_OK;
        bool reconnect = false;

        if (command.opcode == RX3_S3_CONFIG_SET_CREDENTIALS) {
            rx3_wifi_credentials_t previous;
            portENTER_CRITICAL(&s_bridge.lock);
            previous = s_wifi_credentials;
            portEXIT_CRITICAL(&s_bridge.lock);

            const esp_err_t apply_result = apply_wifi_credentials(&command.credentials);
            if (apply_result != ESP_OK) {
                ESP_LOGE(TAG, "Could not apply Wi-Fi credentials: %s", esp_err_to_name(apply_result));
                status = RX3_S3_CONFIG_INTERNAL_ERROR;
            } else {
                const esp_err_t save_result = wifi_credentials_save(&command.credentials);
                if (save_result == ESP_OK) {
                    portENTER_CRITICAL(&s_bridge.lock);
                    s_wifi_credentials = command.credentials;
                    s_wifi_credentials_saved = true;
                    portEXIT_CRITICAL(&s_bridge.lock);
                    reconnect = true;
                } else {
                    ESP_LOGE(TAG, "Could not save Wi-Fi credentials: %s", esp_err_to_name(save_result));
                    const esp_err_t rollback_result = apply_wifi_credentials(&previous);
                    if (rollback_result != ESP_OK) {
                        ESP_LOGE(TAG,
                                 "Could not restore prior Wi-Fi credentials: %s",
                                 esp_err_to_name(rollback_result));
                    }
                    status = RX3_S3_CONFIG_STORAGE_ERROR;
                }
            }
            memset(&previous, 0, sizeof(previous));
        } else if (command.opcode == RX3_S3_CONFIG_RECONNECT) {
            reconnect = true;
        }

        queue_config_response(command.ethernet_addresses,
                              &request,
                              status,
                              NULL,
                              0);
        memset(&command, 0, sizeof(command));
        if (reconnect) {
            vTaskDelay(pdMS_TO_TICKS(100));
            request_wifi_reconnect();
        }
    }
}

static esp_err_t initialize_config_commands(void)
{
    s_config_commands = xQueueCreate(4, sizeof(config_command_t));
    ESP_RETURN_ON_FALSE(s_config_commands != NULL,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "Could not create configuration command queue");

    const BaseType_t created = xTaskCreatePinnedToCore(config_command_task,
                                                       "config-command",
                                                       4096,
                                                       NULL,
                                                       3,
                                                       NULL,
                                                       1);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static bool is_config_frame(const uint8_t *frame, uint16_t length)
{
    static const uint8_t magic[] = {'R', 'X', '3', 'C'};
    return length >= ETHERNET_HEADER_SIZE + sizeof(magic) &&
           frame[12] == (RX3_S3_CONFIG_ETHERTYPE >> 8) &&
           frame[13] == (RX3_S3_CONFIG_ETHERTYPE & 0xff) &&
           memcmp(frame + ETHERNET_HEADER_SIZE, magic, sizeof(magic)) == 0;
}

static void handle_config_frame(const uint8_t *frame, uint16_t length)
{
    const uint8_t *packet = frame + ETHERNET_HEADER_SIZE;
    const size_t packet_length = length - ETHERNET_HEADER_SIZE;
    rx3_config_request_t request;
    if (!rx3_config_parse_request(packet, packet_length, &request)) {
        const rx3_config_request_t malformed = {
            .opcode = packet_length > 5 ? packet[5] : 0,
            .request_id = packet_length > 9 ? ((uint16_t)packet[8] << 8) | packet[9] : 0,
        };
        queue_config_response(frame, &malformed, RX3_S3_CONFIG_BAD_REQUEST, NULL, 0);
        return;
    }

    if (request.opcode == RX3_S3_CONFIG_GET_STATUS) {
        if (request.payload_length == 0) {
            queue_live_status_response(frame, &request);
        } else {
            queue_config_response(frame, &request, RX3_S3_CONFIG_BAD_REQUEST, NULL, 0);
        }
        return;
    }

    config_command_t command = {
        .opcode = request.opcode,
        .request_id = request.request_id,
    };
    memcpy(command.ethernet_addresses, frame, sizeof(command.ethernet_addresses));

    if (request.opcode == RX3_S3_CONFIG_SET_CREDENTIALS) {
        if (!rx3_config_parse_credentials(&request, &command.credentials)) {
            queue_config_response(frame,
                                  &request,
                                  RX3_S3_CONFIG_INVALID_CREDENTIALS,
                                  NULL,
                                  0);
            return;
        }
    } else if (request.opcode != RX3_S3_CONFIG_RECONNECT) {
        queue_config_response(frame, &request, RX3_S3_CONFIG_UNSUPPORTED, NULL, 0);
        return;
    } else if (request.payload_length != 0) {
        queue_config_response(frame, &request, RX3_S3_CONFIG_BAD_REQUEST, NULL, 0);
        return;
    }

    if (xQueueSend(s_config_commands, &command, 0) != pdTRUE) {
        queue_config_response(frame, &request, RX3_S3_CONFIG_INTERNAL_ERROR, NULL, 0);
    }
    memset(&command, 0, sizeof(command));
}

static void wifi_tx_done(uint8_t interface,
                         uint8_t *data,
                         uint16_t *data_length,
                         bool transmitted)
{
    (void)data;
    (void)data_length;

    if (interface != WIFI_IF_STA) {
        return;
    }

    portENTER_CRITICAL(&s_bridge.lock);
    if (transmitted) {
        s_bridge.counters.wifi_tx_successes++;
    } else {
        s_bridge.counters.wifi_tx_failures++;
    }
    portEXIT_CRITICAL(&s_bridge.lock);
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    // The RX3 mounts every VFAT source with -o rw before it invokes
    // decrypt_autoexec.sh. Reporting write protection prevents the mount and
    // makes the bootstrap invisible even though the UI detects the disk.
    return true;
}

static esp_err_t initialize_msc(void)
{
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                 ESP_PARTITION_SUBTYPE_DATA_FAT,
                                                                 "rx3disk");
    if (partition == NULL) {
        ESP_LOGE(TAG, "Could not find rx3disk partition");
        return ESP_ERR_NOT_FOUND;
    }

    if (partition->address != 0x300000 || partition->size != 0x500000) {
        ESP_LOGE(TAG,
                 "Unexpected rx3disk geometry: address=0x%" PRIx32 " size=0x%" PRIx32,
                 partition->address,
                 partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_RETURN_ON_ERROR(wl_mount(partition, &s_msc_wl_handle),
                        TAG,
                        "Could not mount rx3disk wear-level layer");

    const tinyusb_msc_storage_config_t storage = {
        .medium.wl_handle = s_msc_wl_handle,
        .fat_fs = {
            .base_path = NULL,
            .config.max_files = 1,
            .do_not_format = true,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };

    return tinyusb_msc_new_storage_spiflash(&storage, &s_msc_storage);
}

static void set_usb_link(bool link_up)
{
    bool changed = false;

    portENTER_CRITICAL(&s_bridge.lock);
    if (s_bridge.usb_link_up != link_up) {
        s_bridge.usb_link_up = link_up;
        s_bridge.counters.usb_link_changes++;
        changed = true;
    }
    portEXIT_CRITICAL(&s_bridge.lock);

    if (changed) {
        tud_network_link_state(0, link_up);
        ESP_LOGI(TAG, "USB NCM carrier %s", link_up ? "up" : "down");
    }
}

static esp_err_t usb_to_wifi(void *buffer, uint16_t length, void *context)
{
    (void)context;
    const uint8_t *frame = buffer;

    if (is_config_frame(frame, length)) {
        handle_config_frame(frame, length);
        return ESP_OK;
    }

    if (is_control_request(frame,
                           length,
                           BRIDGE_DIAGNOSTIC_REQUEST,
                           sizeof(BRIDGE_DIAGNOSTIC_REQUEST) - 1)) {
        queue_status_response(frame);
        return ESP_OK;
    }

    if (is_control_request(frame,
                           length,
                           BRIDGE_BOOTLOADER_REQUEST,
                           sizeof(BRIDGE_BOOTLOADER_REQUEST) - 1)) {
        queue_control_response(frame, BRIDGE_BOOTLOADER_RESPONSE " rebooting");
        if (s_bootloader_task != NULL) {
            xTaskNotifyGive(s_bootloader_task);
        }
        return ESP_OK;
    }

    bool connected;

    portENTER_CRITICAL(&s_bridge.lock);
    connected = s_bridge.wifi_connected;
    portEXIT_CRITICAL(&s_bridge.lock);

    const esp_err_t result = connected
                                 ? esp_wifi_internal_tx(WIFI_IF_STA, buffer, length)
                                 : ESP_ERR_INVALID_STATE;

    portENTER_CRITICAL(&s_bridge.lock);
    s_bridge.counters.usb_rx_frames++;
    s_bridge.counters.usb_rx_bytes += length;
    s_bridge.counters.last_wifi_tx_result = result;
    if (result != ESP_OK) {
        s_bridge.counters.usb_rx_drops++;
    }
    portEXIT_CRITICAL(&s_bridge.lock);

    return ESP_OK;
}

static void release_usb_tx_frame(void *buffer, void *context)
{
    (void)context;

    if (buffer == NULL || xQueueSend(s_wifi_to_usb_free, &buffer, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Could not return USB transmit frame to pool");
    }
}

static esp_err_t wifi_to_usb(void *buffer, uint16_t length, void *rx_buffer)
{
    bridge_frame_t *frame = NULL;
    const uint16_t usb_length = length < ETHERNET_MIN_FRAME_SIZE
                                    ? ETHERNET_MIN_FRAME_SIZE
                                    : length;
    const bool accepted = usb_length <= BRIDGE_MAX_FRAME_SIZE &&
                          xQueueReceive(s_wifi_to_usb_free, &frame, 0) == pdTRUE;

    if (accepted) {
        frame->length = usb_length;
        memcpy(frame->data, buffer, length);
        if (usb_length > length) {
            memset(frame->data + length, 0, usb_length - length);
        }
    }

    esp_wifi_internal_free_rx_buffer(rx_buffer);

    const bool queued = accepted &&
                        xQueueSend(s_wifi_to_usb_pending, &frame, 0) == pdTRUE;

    portENTER_CRITICAL(&s_bridge.lock);
    s_bridge.counters.wifi_rx_frames++;
    s_bridge.counters.wifi_rx_bytes += length;
    if (!queued) {
        s_bridge.counters.wifi_rx_drops++;
    }
    portEXIT_CRITICAL(&s_bridge.lock);

    if (accepted && !queued) {
        release_usb_tx_frame(frame, NULL);
    }

    return ESP_OK;
}

static void wifi_to_usb_task(void *argument)
{
    (void)argument;

    while (true) {
        bridge_frame_t *frame;
        xQueueReceive(s_wifi_to_usb_pending, &frame, portMAX_DELAY);

        const esp_err_t result = tinyusb_net_send_async(frame->data,
                                                        frame->length,
                                                        frame);
        if (result == ESP_OK) {
            continue;
        }

        portENTER_CRITICAL(&s_bridge.lock);
        s_bridge.counters.wifi_rx_drops++;
        portEXIT_CRITICAL(&s_bridge.lock);
        release_usb_tx_frame(frame, NULL);
    }
}

static esp_err_t initialize_wifi_to_usb_queue(void)
{
    s_wifi_to_usb_free = xQueueCreate(CONFIG_RX3_LINK_WIFI_TO_USB_QUEUE_DEPTH,
                                      sizeof(bridge_frame_t *));
    s_wifi_to_usb_pending = xQueueCreate(CONFIG_RX3_LINK_WIFI_TO_USB_QUEUE_DEPTH,
                                         sizeof(bridge_frame_t *));
    ESP_RETURN_ON_FALSE(s_wifi_to_usb_free != NULL && s_wifi_to_usb_pending != NULL,
                        ESP_ERR_NO_MEM,
                        TAG,
                        "Could not create Wi-Fi-to-USB queues");

    for (size_t index = 0; index < CONFIG_RX3_LINK_WIFI_TO_USB_QUEUE_DEPTH; index++) {
        bridge_frame_t *frame = &s_wifi_to_usb_frames[index];
        ESP_RETURN_ON_FALSE(xQueueSend(s_wifi_to_usb_free, &frame, 0) == pdTRUE,
                            ESP_FAIL,
                            TAG,
                            "Could not populate Wi-Fi-to-USB frame pool");
    }

    BaseType_t created = xTaskCreatePinnedToCore(wifi_to_usb_task,
                                                 "wifi-to-usb",
                                                 4096,
                                                 NULL,
                                                 4,
                                                 NULL,
                                                 1);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

static void wifi_event(void *argument,
                       esp_event_base_t event_base,
                       int32_t event_id,
                       void *event_data)
{
    (void)argument;
    (void)event_data;

    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_ERROR_CHECK(esp_wifi_internal_reg_rxcb(WIFI_IF_STA, wifi_to_usb));

        portENTER_CRITICAL(&s_bridge.lock);
        s_bridge.wifi_connected = true;
        portEXIT_CRITICAL(&s_bridge.lock);

        ESP_LOGI(TAG, "Wi-Fi associated; transparent bridge enabled");
        return;
    }

    if (event_id != WIFI_EVENT_STA_DISCONNECTED) {
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_internal_reg_rxcb(WIFI_IF_STA, NULL));

    portENTER_CRITICAL(&s_bridge.lock);
    s_bridge.wifi_connected = false;
    s_bridge.counters.wifi_reconnects++;
    portEXIT_CRITICAL(&s_bridge.lock);

    ESP_LOGW(TAG, "Wi-Fi disconnected; retrying association");
    esp_err_t result = esp_wifi_connect();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi reconnect request failed: %s", esp_err_to_name(result));
    }
}

static esp_err_t initialize_nvs(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Could not erase NVS");
        result = nvs_flash_init();
    }

    return result;
}

static esp_err_t initialize_wifi(uint8_t bridge_mac[BRIDGE_MAC_LENGTH],
                                 const rx3_wifi_credentials_t *credentials)
{
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Could not create event loop");

    wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&initialization), TAG, "Could not initialize Wi-Fi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM),
                        TAG,
                        "Could not select volatile ESP Wi-Fi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Could not select station mode");

    if (CONFIG_RX3_LINK_WIFI_MAC[0] != '\0') {
        if (!bridge_parse_mac(CONFIG_RX3_LINK_WIFI_MAC, bridge_mac)) {
            ESP_LOGE(TAG, "Configured MAC must be a locally administered unicast address");
            return ESP_ERR_INVALID_ARG;
        }
        ESP_RETURN_ON_ERROR(esp_wifi_set_mac(WIFI_IF_STA, bridge_mac), TAG, "Could not set station MAC");
    } else {
        ESP_RETURN_ON_ERROR(esp_read_mac(bridge_mac, ESP_MAC_WIFI_STA), TAG, "Could not read station MAC");
    }

    ESP_RETURN_ON_ERROR(apply_wifi_credentials(credentials), TAG, "Could not configure station");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT,
                                                    ESP_EVENT_ANY_ID,
                                                    wifi_event,
                                                    NULL),
                        TAG,
                        "Could not register Wi-Fi events");

    return ESP_OK;
}

static esp_err_t initialize_network(void)
{
    tinyusb_net_config_t network = {
        .on_recv_callback = usb_to_wifi,
        .free_tx_buffer = release_usb_tx_frame,
        .user_context = &s_bridge,
    };
    memcpy(network.mac_addr, s_bridge_mac, sizeof(network.mac_addr));

    ESP_RETURN_ON_ERROR(tinyusb_net_init(&network), TAG, "Could not initialize NCM");
    // Configuration must remain reachable when Wi-Fi credentials are wrong.
    // Wi-Fi state gates transparent frame forwarding, not the point-to-point
    // NCM carrier used by the RX3 control protocol.
    set_usb_link(true);

    return ESP_OK;
}

static esp_err_t initialize_bootstrap_usb(void)
{
    ESP_RETURN_ON_ERROR(initialize_msc(), TAG, "Could not initialize bootstrap disk");

    tinyusb_config_t usb = TINYUSB_DEFAULT_CONFIG();
    usb.descriptor.full_speed_config = s_rx3_boot_usb_config;
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&usb), TAG, "Could not install TinyUSB");
    return initialize_network();
}

static void bootloader_task(void *argument)
{
    (void)argument;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(500));

        ESP_LOGW(TAG, "Entering ROM USB download mode");
        chip_usb_set_persist_flags(USBDC_PERSIST_ENA);
        REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
        esp_restart();
    }
}

static void stats_task(void *argument)
{
    (void)argument;
    const TickType_t interval = pdMS_TO_TICKS(CONFIG_RX3_LINK_STATS_INTERVAL_SECONDS * 1000);

    while (true) {
        vTaskDelay(interval);

        bridge_counters_t counters;
        bool connected;
        bool link_up;

        portENTER_CRITICAL(&s_bridge.lock);
        counters = s_bridge.counters;
        connected = s_bridge.wifi_connected;
        link_up = s_bridge.usb_link_up;
        portEXIT_CRITICAL(&s_bridge.lock);

        ESP_LOGI(TAG,
                 "stats wifi=%s ncm=%s usb_rx=%" PRIu64 "/%" PRIu64
                 "B usb_drop=%" PRIu64 " wifi_rx=%" PRIu64 "/%" PRIu64
                 "B wifi_drop=%" PRIu64 " tx_ok=%" PRIu64 " tx_fail=%" PRIu64
                 " reconnect=%" PRIu64 " link_change=%" PRIu64,
                 connected ? "up" : "down",
                 link_up ? "up" : "down",
                 counters.usb_rx_frames,
                 counters.usb_rx_bytes,
                 counters.usb_rx_drops,
                 counters.wifi_rx_frames,
                 counters.wifi_rx_bytes,
                 counters.wifi_rx_drops,
                 counters.wifi_tx_successes,
                 counters.wifi_tx_failures,
                 counters.wifi_reconnects,
                 counters.usb_link_changes);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(initialize_nvs());
    ESP_ERROR_CHECK(wifi_credentials_load(&s_wifi_credentials, &s_wifi_credentials_saved));
    ESP_ERROR_CHECK(initialize_wifi(s_bridge_mac, &s_wifi_credentials));
    ESP_ERROR_CHECK(initialize_wifi_to_usb_queue());
    ESP_ERROR_CHECK(initialize_config_commands());

    BaseType_t bootloader_created = xTaskCreate(bootloader_task,
                                                "bootloader",
                                                2048,
                                                NULL,
                                                5,
                                                &s_bootloader_task);
    ESP_ERROR_CHECK(bootloader_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG,
             "shared Wi-Fi/NCM MAC %02x:%02x:%02x:%02x:%02x:%02x",
             s_bridge_mac[0],
             s_bridge_mac[1],
             s_bridge_mac[2],
             s_bridge_mac[3],
             s_bridge_mac[4],
             s_bridge_mac[5]);

    ESP_ERROR_CHECK(initialize_bootstrap_usb());
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_tx_done_cb(wifi_tx_done));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_connect());

    BaseType_t created = xTaskCreate(stats_task, "bridge-stats", 4096, NULL, 2, NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
