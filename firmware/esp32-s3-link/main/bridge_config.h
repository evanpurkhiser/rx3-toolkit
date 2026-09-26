#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BRIDGE_MAC_LENGTH 6

bool bridge_parse_mac(const char *text, uint8_t mac[BRIDGE_MAC_LENGTH]);
bool bridge_mac_is_local_unicast(const uint8_t mac[BRIDGE_MAC_LENGTH]);
