#include "config_protocol.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static size_t write_request(uint8_t *packet,
                            uint8_t opcode,
                            uint16_t request_id,
                            const uint8_t *payload,
                            uint16_t payload_length)
{
    memcpy(packet, "RX3C", 4);
    packet[4] = opcode;
    packet[5] = 0;
    packet[6] = request_id >> 8;
    packet[7] = request_id & 0xff;
    packet[8] = payload_length >> 8;
    packet[9] = payload_length & 0xff;
    if (payload_length != 0) {
        memcpy(packet + RX3_S3_CONFIG_HEADER_SIZE, payload, payload_length);
    }
    return RX3_S3_CONFIG_HEADER_SIZE + payload_length;
}

static void test_request_header(void)
{
    uint8_t packet[64];
    const size_t length = write_request(packet, RX3_S3_CONFIG_GET_STATUS, 0x1234, NULL, 0);
    rx3_config_request_t request;
    assert(rx3_config_parse_request(packet, length, &request));
    assert(request.opcode == RX3_S3_CONFIG_GET_STATUS);
    assert(request.request_id == 0x1234);
    assert(request.payload_length == 0);

    packet[0] = 'B';
    assert(!rx3_config_parse_request(packet, length, &request));
    packet[0] = 'R';
    packet[5] = 1;
    assert(!rx3_config_parse_request(packet, length, &request));
    packet[5] = 0;
    packet[8] = 0;
    packet[9] = 1;
    assert(!rx3_config_parse_request(packet, length, &request));
}

static void test_ethernet_padding_is_ignored(void)
{
    uint8_t packet[60] = {0};
    write_request(packet, RX3_S3_CONFIG_RECONNECT, 7, NULL, 0);
    rx3_config_request_t request;
    assert(rx3_config_parse_request(packet, sizeof(packet), &request));
    assert(request.payload_length == 0);
}

static void test_credentials(void)
{
    uint8_t packet[RX3_CONFIG_MAX_PACKET_SIZE];
    uint8_t payload[2 + RX3_CONFIG_SSID_MAX_LENGTH + RX3_CONFIG_PASSWORD_MAX_LENGTH] = {
        4, 8, 't', 'e', 's', 't', 'p', 'a', 's', 's', 'w', 'o', 'r', 'd',
    };
    size_t length = write_request(packet,
                                  RX3_S3_CONFIG_SET_CREDENTIALS,
                                  9,
                                  payload,
                                  14);
    rx3_config_request_t request;
    rx3_wifi_credentials_t credentials;
    assert(rx3_config_parse_request(packet, length, &request));
    assert(rx3_config_parse_credentials(&request, &credentials));
    assert(strcmp(credentials.ssid, "test") == 0);
    assert(strcmp(credentials.password, "password") == 0);

    packet[RX3_S3_CONFIG_HEADER_SIZE] = 0;
    assert(!rx3_config_parse_credentials(&request, &credentials));
    packet[RX3_S3_CONFIG_HEADER_SIZE] = 4;
    packet[RX3_S3_CONFIG_HEADER_SIZE + 1] = 7;
    assert(!rx3_config_parse_credentials(&request, &credentials));
    packet[RX3_S3_CONFIG_HEADER_SIZE + 1] = 64;
    assert(!rx3_config_parse_credentials(&request, &credentials));
}

static void test_response_and_status(void)
{
    const rx3_config_request_t request = {
        .opcode = RX3_S3_CONFIG_GET_STATUS,
        .request_id = 0xabcd,
    };
    const rx3_config_live_status_t status = {
        .flags = RX3_S3_CONFIG_FLAG_WIFI_CONNECTED |
                 RX3_S3_CONFIG_FLAG_PASSWORD_SET,
        .rssi = -42,
        .mac = {0x02, 0x52, 0x58, 0x33, 0x00, 0x01},
        .ssid_length = 4,
        .ssid = "test",
    };
    uint8_t status_payload[RX3_CONFIG_STATUS_PAYLOAD_SIZE];
    const size_t status_length = rx3_config_write_status_payload(status_payload,
                                                                  sizeof(status_payload),
                                                                  &status);
    assert(status_length == 13);
    assert(status_payload[1] == (uint8_t)-42);
    assert(status_payload[8] == 4);
    assert(memcmp(status_payload + 9, "test", 4) == 0);

    uint8_t response[64];
    const size_t response_length = rx3_config_write_response(response,
                                                              sizeof(response),
                                                              &request,
                                                              RX3_S3_CONFIG_OK,
                                                              status_payload,
                                                              status_length);
    assert(response_length == RX3_S3_CONFIG_HEADER_SIZE + status_length);
    assert(memcmp(response, "RX3C", 4) == 0);
    assert(response[4] == (RX3_S3_CONFIG_GET_STATUS | RX3_S3_CONFIG_RESPONSE_BIT));
    assert(response[5] == RX3_S3_CONFIG_OK);
    assert(response[6] == 0xab && response[7] == 0xcd);
    assert(response[8] == 0 && response[9] == status_length);
    assert(memmem(response, response_length, "password", 8) == NULL);
}

int main(void)
{
    test_request_header();
    test_ethernet_padding_is_ignored();
    test_credentials();
    test_response_and_status();
    return 0;
}
