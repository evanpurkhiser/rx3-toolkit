#include "config_protocol.h"

#include <string.h>

static const uint8_t protocol_magic[4] = {'R', 'X', '3', 'C'};

static uint16_t read_u16(const uint8_t *bytes)
{
    return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = value >> 8;
    bytes[1] = value & 0xff;
}

bool rx3_config_parse_request(const uint8_t *packet,
                              size_t packet_length,
                              rx3_config_request_t *request)
{
    if (packet == NULL || request == NULL || packet_length < RX3_CONFIG_HEADER_SIZE) {
        return false;
    }

    const uint16_t payload_length = read_u16(packet + 8);
    if (memcmp(packet, protocol_magic, sizeof(protocol_magic)) != 0 ||
        packet[5] != 0 ||
        payload_length > packet_length - RX3_CONFIG_HEADER_SIZE) {
        return false;
    }

    request->opcode = packet[4];
    request->request_id = read_u16(packet + 6);
    request->payload = packet + RX3_CONFIG_HEADER_SIZE;
    request->payload_length = payload_length;
    return true;
}

bool rx3_config_parse_credentials(const rx3_config_request_t *request,
                                  rx3_wifi_credentials_t *credentials)
{
    if (request == NULL || credentials == NULL || request->payload_length < 2) {
        return false;
    }

    const uint8_t ssid_length = request->payload[0];
    const uint8_t password_length = request->payload[1];
    if (ssid_length == 0 || ssid_length > RX3_CONFIG_SSID_MAX_LENGTH ||
        password_length > RX3_CONFIG_PASSWORD_MAX_LENGTH ||
        (password_length > 0 && password_length < 8) ||
        request->payload_length != (uint16_t)(2 + ssid_length + password_length)) {
        return false;
    }

    memset(credentials, 0, sizeof(*credentials));
    credentials->ssid_length = ssid_length;
    credentials->password_length = password_length;
    memcpy(credentials->ssid, request->payload + 2, ssid_length);
    memcpy(credentials->password, request->payload + 2 + ssid_length, password_length);
    return true;
}

size_t rx3_config_write_response(uint8_t *packet,
                                 size_t capacity,
                                 const rx3_config_request_t *request,
                                 rx3_s3_config_status_code_t status,
                                 const uint8_t *payload,
                                 uint16_t payload_length)
{
    if (packet == NULL || request == NULL ||
        capacity < (size_t)RX3_CONFIG_HEADER_SIZE + payload_length ||
        (payload_length != 0 && payload == NULL)) {
        return 0;
    }

    memcpy(packet, protocol_magic, sizeof(protocol_magic));
    packet[4] = request->opcode | RX3_S3_CONFIG_RESPONSE_BIT;
    packet[5] = status;
    write_u16(packet + 6, request->request_id);
    write_u16(packet + 8, payload_length);
    if (payload_length != 0) {
        memcpy(packet + RX3_CONFIG_HEADER_SIZE, payload, payload_length);
    }

    return RX3_CONFIG_HEADER_SIZE + payload_length;
}

size_t rx3_config_write_status_payload(uint8_t *payload,
                                       size_t capacity,
                                       const rx3_config_live_status_t *status)
{
    if (payload == NULL || status == NULL ||
        status->ssid_length > RX3_CONFIG_SSID_MAX_LENGTH ||
        capacity < (size_t)RX3_S3_CONFIG_STATUS_FIXED_SIZE + status->ssid_length) {
        return 0;
    }

    payload[0] = status->flags;
    payload[1] = (uint8_t)status->rssi;
    memcpy(payload + 2, status->mac, sizeof(status->mac));
    payload[8] = status->ssid_length;
    memcpy(payload + 9, status->ssid, status->ssid_length);
    return 9 + status->ssid_length;
}
