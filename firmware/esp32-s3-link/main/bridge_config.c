#include "bridge_config.h"

#include <stdio.h>

bool bridge_mac_is_local_unicast(const uint8_t mac[BRIDGE_MAC_LENGTH])
{
    return (mac[0] & 0x01U) == 0 && (mac[0] & 0x02U) != 0;
}
bool bridge_parse_mac(const char *text, uint8_t mac[BRIDGE_MAC_LENGTH])
{
    unsigned int values[BRIDGE_MAC_LENGTH];
    char trailing;

    if (text == NULL || text[0] == '\0') {
        return false;
    }

    const int fields = sscanf(text,
                              "%2x:%2x:%2x:%2x:%2x:%2x%c",
                              &values[0],
                              &values[1],
                              &values[2],
                              &values[3],
                              &values[4],
                              &values[5],
                              &trailing);
    if (fields != BRIDGE_MAC_LENGTH) {
        return false;
    }

    for (size_t index = 0; index < BRIDGE_MAC_LENGTH; index++) {
        mac[index] = (uint8_t)values[index];
    }

    return bridge_mac_is_local_unicast(mac);
}
