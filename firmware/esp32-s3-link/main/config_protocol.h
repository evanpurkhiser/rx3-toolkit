#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rx3_s3_config_protocol.h"

#define RX3_CONFIG_HEADER_SIZE RX3_S3_CONFIG_HEADER_SIZE
#define RX3_CONFIG_SSID_MAX_LENGTH RX3_S3_CONFIG_SSID_MAX_LENGTH
#define RX3_CONFIG_PASSWORD_MAX_LENGTH RX3_S3_CONFIG_PASSWORD_MAX_LENGTH
#define RX3_CONFIG_STATUS_PAYLOAD_SIZE \
    (RX3_S3_CONFIG_STATUS_FIXED_SIZE + RX3_S3_CONFIG_SSID_MAX_LENGTH)
#define RX3_CONFIG_MAX_PACKET_SIZE \
    (RX3_CONFIG_HEADER_SIZE + 2 + RX3_CONFIG_SSID_MAX_LENGTH + RX3_CONFIG_PASSWORD_MAX_LENGTH)

typedef struct {
    uint8_t opcode;
    uint16_t request_id;
    const uint8_t *payload;
    uint16_t payload_length;
} rx3_config_request_t;

typedef struct {
    uint8_t ssid_length;
    uint8_t password_length;
    char ssid[RX3_CONFIG_SSID_MAX_LENGTH + 1];
    char password[RX3_CONFIG_PASSWORD_MAX_LENGTH + 1];
} rx3_wifi_credentials_t;

typedef struct {
    uint8_t flags;
    int8_t rssi;
    uint8_t mac[6];
    uint8_t ssid_length;
    char ssid[RX3_CONFIG_SSID_MAX_LENGTH + 1];
} rx3_config_live_status_t;

bool rx3_config_parse_request(const uint8_t *packet,
                              size_t packet_length,
                              rx3_config_request_t *request);
bool rx3_config_parse_credentials(const rx3_config_request_t *request,
                                  rx3_wifi_credentials_t *credentials);
size_t rx3_config_write_response(uint8_t *packet,
                                 size_t capacity,
                                 const rx3_config_request_t *request,
                                 rx3_s3_config_status_code_t status,
                                 const uint8_t *payload,
                                 uint16_t payload_length);
size_t rx3_config_write_status_payload(uint8_t *payload,
                                       size_t capacity,
                                       const rx3_config_live_status_t *status);
