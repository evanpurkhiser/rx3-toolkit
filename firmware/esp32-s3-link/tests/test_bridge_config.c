#include "bridge_config.h"

#include <assert.h>
#include <stdint.h>

static void expect_valid(const char *text, const uint8_t expected[BRIDGE_MAC_LENGTH])
{
    uint8_t parsed[BRIDGE_MAC_LENGTH];
    assert(bridge_parse_mac(text, parsed));

    for (int index = 0; index < BRIDGE_MAC_LENGTH; index++) {
        assert(parsed[index] == expected[index]);
    }
}

int main(void)
{
    const uint8_t expected[BRIDGE_MAC_LENGTH] = {0x02, 0x52, 0x58, 0x33, 0x00, 0x01};
    uint8_t parsed[BRIDGE_MAC_LENGTH];

    expect_valid("02:52:58:33:00:01", expected);
    assert(!bridge_parse_mac("", parsed));
    assert(!bridge_parse_mac("02:52:58:33:00", parsed));
    assert(!bridge_parse_mac("02:52:58:33:00:01:02", parsed));
    assert(!bridge_parse_mac("01:52:58:33:00:01", parsed));
    assert(!bridge_parse_mac("00:52:58:33:00:01", parsed));

    return 0;
}
