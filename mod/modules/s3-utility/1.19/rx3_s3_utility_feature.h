/* SPDX-License-Identifier: MPL-2.0
 * ESP32-S3 rows contributed to the core utility-menu broker.
 */

#ifndef RX3_S3_UTILITY_FEATURE_H
#define RX3_S3_UTILITY_FEATURE_H

#include "rx3_s3_config_client.h"

static int s3_utility_configured(void)
{
    const char *enabled = getenv("RX3_S3_UTILITY");
    return enabled && enabled[0] == '1';
}

static void s3_utility_status(char *output, unsigned int capacity)
{
    struct rx3_s3_snapshot snapshot;
    rx3_s3_read_snapshot(&snapshot);
    if (!snapshot.reachable) {
        rx3_s3_copy_text(output, capacity, "UNAVAILABLE", 11u);
        return;
    }
    if (!snapshot.wifi_connected) {
        rx3_s3_copy_text(output, capacity, "DISCONNECTED", 12u);
        return;
    }
    rx3_s3_copy_text(output, capacity, "CONNECTED  RSSI ", 16u);
    unsigned int offset = rx3_s3_string_length(output, capacity);
    int rssi = snapshot.rssi;
    if (rssi < 0 && offset + 1u < capacity) {
        output[offset++] = '-';
        rssi = -rssi;
    }
    (void)rx3_s3_append_unsigned(output, offset, capacity, (unsigned int)rssi);
}

static void s3_utility_ssid(char *output, unsigned int capacity)
{
    struct rx3_s3_snapshot snapshot;
    rx3_s3_read_snapshot(&snapshot);
    if (!snapshot.reachable) {
        rx3_s3_copy_text(output, capacity, "UNAVAILABLE", 11u);
        return;
    }
    const char *value = snapshot.ssid[0] ? snapshot.ssid : "NOT SET";
    rx3_s3_copy_text(output, capacity, value,
                     rx3_s3_string_length(value, sizeof(snapshot.ssid)));
}

static void s3_utility_password(char *output, unsigned int capacity)
{
    struct rx3_s3_snapshot snapshot;
    rx3_s3_read_snapshot(&snapshot);
    if (!snapshot.reachable) {
        rx3_s3_copy_text(output, capacity, "UNAVAILABLE", 11u);
        return;
    }
    const char *value = snapshot.password_set ? "SET" : "NOT SET";
    rx3_s3_copy_text(output, capacity, value,
                     rx3_s3_string_length(value, 8u));
}

static void s3_utility_ip(char *output, unsigned int capacity)
{
    struct rx3_s3_snapshot snapshot;
    rx3_s3_read_snapshot(&snapshot);
    if (!snapshot.reachable) {
        rx3_s3_copy_text(output, capacity, "UNAVAILABLE", 11u);
        return;
    }
    rx3_s3_format_ipv4(output, capacity, snapshot.ipv4);
}

static void s3_utility_mac(char *output, unsigned int capacity)
{
    struct rx3_s3_snapshot snapshot;
    rx3_s3_read_snapshot(&snapshot);
    if (!snapshot.reachable) {
        rx3_s3_copy_text(output, capacity, "UNAVAILABLE", 11u);
        return;
    }
    rx3_s3_format_mac(output, capacity, snapshot.mac);
}

static const struct rx3_utility_item s3_utility_items[] = {
    {RX3_UTILITY_SECTION, "ESP32-S3 INTEGRATION", 0},
    {RX3_UTILITY_VALUE, "      WI-FI STATUS", s3_utility_status},
    {RX3_UTILITY_VALUE, "      SSID", s3_utility_ssid},
    {RX3_UTILITY_VALUE, "      PASSWORD", s3_utility_password},
    {RX3_UTILITY_VALUE, "      LINK IP", s3_utility_ip},
    {RX3_UTILITY_VALUE, "      ADAPTER MAC", s3_utility_mac},
};

static const struct rx3_utility_extension s3_utility_extension = {
    "s3-utility",
    s3_utility_configured,
    rx3_s3_client_start,
    rx3_s3_client_stop,
    sizeof(s3_utility_items) / sizeof(s3_utility_items[0]),
    s3_utility_items,
};

#endif /* RX3_S3_UTILITY_FEATURE_H */
