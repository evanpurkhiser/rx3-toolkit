/* SPDX-License-Identifier: MPL-2.0
 * One guarded owner for firmware 1.19's native utility descriptor table.
 */

#ifndef RX3_UTILITY_BROKER_H
#define RX3_UTILITY_BROKER_H

#include "../../s3-utility/1.19/rx3_s3_utility_feature.h"

#define RX3_STOCK_UTILITY_COUNT 33u
#define RX3_UTILITY_CAPACITY 49u
#define RX3_UTILITY_TEXT_CAPACITY 64u
#define RX3_UTILITY_TABLE_ADDRESS ((unsigned long)0x005140b8)
#define RX3_UTILITY_ITEMS_ADDRESS ((unsigned long)0x005140bc)
#define RX3_UTILITY_GENERAL_INDEX 25u
#define RX3_UTILITY_VERSION_INDEX 32u

struct rx3_native_utility_item {
    const uint16_t *label;
    const uint16_t **values;
    uint32_t value_count;
    int32_t minimum;
    int32_t maximum;
    uint32_t editable;
    void *callbacks[8];
};

struct rx3_native_utility_table {
    uint32_t count;
    struct rx3_native_utility_item items[RX3_UTILITY_CAPACITY];
};

struct rx3_utility_binding {
    const struct rx3_utility_item *item;
    uint16_t label[RX3_UTILITY_TEXT_CAPACITY];
    uint16_t value[RX3_UTILITY_TEXT_CAPACITY];
};

_Static_assert(sizeof(struct rx3_native_utility_item) == 0x38u,
               "firmware 1.19 utility descriptor size changed");

static struct rx3_native_utility_table rx3_utility_table;
static struct rx3_utility_binding rx3_utility_bindings[RX3_UTILITY_CAPACITY];
static unsigned int rx3_utility_table_active;

static const unsigned long rx3_utility_literal_slots[] = {
    0x0013c9a8u,
    0x0013cbccu,
    0x0013cd1cu,
    0x0013cf30u,
    0x0013cfc4u,
    0x0013d9ecu,
};

static const struct rx3_utility_extension *rx3_utility_extensions[] = {
    &s3_utility_extension,
};

static unsigned int rx3_utility_ascii_to_utf16(uint16_t *destination,
                                                const char *source)
{
    unsigned int length = 0;
    if (!source)
        source = "";
    while (source[length] && length + 1u < RX3_UTILITY_TEXT_CAPACITY) {
        destination[length] = (uint8_t)source[length];
        length++;
    }
    destination[length] = 0;
    return length;
}

static void rx3_utility_write_line(void *line, const uint16_t *text)
{
    uint8_t *bytes = line;
    unsigned int length = 0;
    while (text[length] && length + 1u < RX3_UTILITY_TEXT_CAPACITY)
        length++;
    memset(bytes + 0x22u, 0, 0x200u);
    *(uint16_t *)(bytes + 0x20u) = (uint16_t)length;
    memcpy(bytes + 0x22u, text, length * sizeof(uint16_t));
}

static int rx3_utility_set_value_line(void *native_item, void *line_pair)
{
    struct rx3_native_utility_item *descriptor = native_item;
    unsigned int index = (unsigned int)(descriptor - rx3_utility_table.items);
    if (index >= rx3_utility_table.count)
        return 0;

    struct rx3_utility_binding *binding = &rx3_utility_bindings[index];
    const char *value = binding->item && binding->item->value
                            ? binding->item->value()
                            : "";
    rx3_utility_ascii_to_utf16(binding->value, value);

    void **lines = line_pair;
    rx3_utility_write_line(lines[0], descriptor->label);
    ((uint8_t *)lines[0])[3] |= 0x20u;
    rx3_utility_write_line(lines[1], binding->value);
    return 0;
}

static int rx3_utility_patch_literals(uint32_t replacement)
{
    unsigned int patched = 0;
    const uint32_t stock = (uint32_t)RX3_UTILITY_TABLE_ADDRESS;
    const unsigned int count = sizeof(rx3_utility_literal_slots) /
                               sizeof(rx3_utility_literal_slots[0]);

    for (unsigned int i = 0; i < count; i++) {
        uint32_t *slot = (uint32_t *)rx3_utility_literal_slots[i];
        if (*slot != stock || write_code((unsigned long)slot, &replacement,
                                         sizeof(replacement))) {
            for (unsigned int undo = 0; undo < patched; undo++)
                (void)write_code(rx3_utility_literal_slots[undo], &stock,
                                 sizeof(stock));
            return 0;
        }
        patched++;
    }
    return 1;
}

static unsigned int rx3_install_utility_extensions(void)
{
    const uint32_t stock_count = *(const uint32_t *)RX3_UTILITY_TABLE_ADDRESS;
    if (stock_count != RX3_STOCK_UTILITY_COUNT) {
        log_number("utility broker rejected: stock item count = ", stock_count);
        return 0;
    }

    rx3_utility_table.count = stock_count;
    memcpy(rx3_utility_table.items,
           (const void *)RX3_UTILITY_ITEMS_ADDRESS,
           stock_count * sizeof(rx3_utility_table.items[0]));
    memset(rx3_utility_bindings, 0, sizeof(rx3_utility_bindings));

    unsigned int enabled = 0;
    const unsigned int extension_count = sizeof(rx3_utility_extensions) /
                                         sizeof(rx3_utility_extensions[0]);
    for (unsigned int extension_index = 0;
         extension_index < extension_count;
         extension_index++) {
        const struct rx3_utility_extension *extension =
            rx3_utility_extensions[extension_index];
        if (!extension->configured || !extension->configured())
            continue;
        if (rx3_utility_table.count + extension->item_count >
            RX3_UTILITY_CAPACITY) {
            log_line("utility broker rejected: extension capacity exceeded");
            return 0;
        }

        for (unsigned int item_index = 0;
             item_index < extension->item_count;
             item_index++) {
            const struct rx3_utility_item *item = &extension->items[item_index];
            const unsigned int native_index = rx3_utility_table.count++;
            struct rx3_native_utility_item *native =
                &rx3_utility_table.items[native_index];
            struct rx3_utility_binding *binding =
                &rx3_utility_bindings[native_index];

            const unsigned int donor = item->kind == RX3_UTILITY_SECTION
                                           ? RX3_UTILITY_GENERAL_INDEX
                                           : RX3_UTILITY_VERSION_INDEX;
            *native = rx3_utility_table.items[donor];
            binding->item = item;
            rx3_utility_ascii_to_utf16(binding->label, item->label);
            native->label = binding->label;
            if (item->kind == RX3_UTILITY_VALUE)
                native->callbacks[1] = (void *)rx3_utility_set_value_line;
        }
        enabled++;
    }

    if (!enabled)
        return 0;

    const uint32_t replacement =
        (uint32_t)(unsigned long)&rx3_utility_table;
    if (!rx3_utility_patch_literals(replacement)) {
        log_line("utility broker rejected: literal guard failed");
        return 0;
    }

    rx3_utility_table_active = 1u;
    log_number("utility broker active: native item count = ",
               rx3_utility_table.count);
    return enabled;
}

static void rx3_remove_utility_extensions(void)
{
    if (!rx3_utility_table_active)
        return;

    const uint32_t replacement = (uint32_t)RX3_UTILITY_TABLE_ADDRESS;
    const uint32_t active = (uint32_t)(unsigned long)&rx3_utility_table;
    const unsigned int count = sizeof(rx3_utility_literal_slots) /
                               sizeof(rx3_utility_literal_slots[0]);
    for (unsigned int i = 0; i < count; i++) {
        uint32_t *slot = (uint32_t *)rx3_utility_literal_slots[i];
        if (*slot == active)
            (void)write_code((unsigned long)slot, &replacement,
                             sizeof(replacement));
    }
    rx3_utility_table_active = 0u;
}

#endif /* RX3_UTILITY_BROKER_H */
