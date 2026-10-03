/* SPDX-License-Identifier: MPL-2.0 */
/* Read-only Utility menu extensions for XDJ-RX3 firmware 1.19. */
#include "../api/rx3_platform.h"
#include "../firmware/rx3_patch.h"
#include "rx3_log.h"
#include "rx3_menu.h"

#ifndef RX3_TOOLKIT_VERSION
#define RX3_TOOLKIT_VERSION "0.0.0-dev"
#endif

#define STOCK_COUNT 33u
#define MENU_CAPACITY 64u
#define TEXT_CAPACITY 64u
#define VALUE_CAPACITY 128u
#define STOCK_COUNT_ADDRESS ((unsigned long)0x005140b8)
#define STOCK_TABLE_ADDRESS ((unsigned long)0x005140bc)

static const unsigned long count_literals[] = {
    0x0013c9a8u, 0x0013cbccu, 0x0013cd1cu,
    0x0013cf30u, 0x0013cfc4u, 0x0013d9ecu,
};
static const unsigned long table_literals[] = {0x0013d9e4u};

enum value_kind { VALUE_LITERAL, VALUE_FILE, VALUE_FIELD };

struct utility_item {
    const uint16_t *label;
    const uint16_t **choices;
    uint32_t choice_count;
    int value;
    int pending_value;
    uint32_t editable;
    void (*initialize)(struct utility_item *);
    void (*render)(struct utility_item *, int *);
    int (*start_edit)(struct utility_item *);
    int (*select)(struct utility_item *, int);
    int (*modified)(struct utility_item *);
    int (*enter)(struct utility_item *);
    int (*grey)(struct utility_item *);
    void (*reset)(struct utility_item *);
};

typedef char utility_item_matches_firmware[
    sizeof(struct utility_item) == 0x38u ? 1 : -1
];

struct value_source {
    enum value_kind kind;
    char value[VALUE_CAPACITY];
    char key[TEXT_CAPACITY];
};

struct custom_item {
    struct utility_item utility;
    uint16_t label[TEXT_CAPACITY];
    struct value_source source;
    char cached_value[VALUE_CAPACITY];
    volatile uint32_t cache_generation;
};

struct definition {
    char section[TEXT_CAPACITY];
    char label[TEXT_CAPACITY];
    struct value_source source;
};

struct registry {
    uint32_t count;
    struct utility_item items[MENU_CAPACITY];
};

static struct registry registry;
static struct custom_item custom_items[MENU_CAPACITY - STOCK_COUNT];
static struct definition definitions[MENU_CAPACITY - STOCK_COUNT];
static uint16_t heading_labels[MENU_CAPACITY - STOCK_COUNT][TEXT_CAPACITY];
static uint32_t definition_count;
static uint32_t custom_count;
static uint32_t heading_count;
static uint32_t patched_counts;
static uint32_t patched_tables;
static int overflow;
static int installed;
static int dynamic_values;
static volatile int updater_running;
static int updater_started;
static pthread_t updater_thread;

static size_t string_length(const char *value)
{
    size_t length = 0;
    while (value[length]) length++;
    return length;
}

static size_t copy_ascii(char *destination, size_t capacity,
                         const char *source, size_t length)
{
    size_t count = length < capacity ? length : capacity - 1u;
    memcpy(destination, source, count);
    destination[count] = '\0';
    return count;
}

static void ascii_to_utf16(uint16_t *destination, size_t capacity,
                           const char *source)
{
    size_t index = 0;
    while (source[index] && index + 1u < capacity) {
        destination[index] = (uint8_t)source[index];
        index++;
    }
    destination[index] = 0;
}

static void set_line(int line, const uint16_t *text)
{
    uint16_t length = 0;
    while (text[length] && length < 255u) length++;
    memset((void *)(line + 0x22), 0, 0x200u);
    *(uint16_t *)(line + 0x20) = length;
    memcpy((void *)(line + 0x22), text, (size_t)length * 2u);
}

static size_t read_file(const char *path, char *buffer, size_t capacity)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t count = read(fd, buffer, capacity - 1u);
    close(fd);
    if (count <= 0) return 0;
    size_t length = (size_t)count;
    while (length && (buffer[length - 1u] == '\n' ||
                      buffer[length - 1u] == '\r')) length--;
    buffer[length] = '\0';
    return length;
}

static size_t read_field(const struct value_source *source, char *value,
                         size_t capacity)
{
    char contents[512];
    size_t length = read_file(source->value, contents, sizeof(contents));
    size_t key_length = string_length(source->key);
    for (size_t offset = 0; offset < length;) {
        size_t end = offset;
        while (end < length && contents[end] != '\n') end++;
        if (end > offset + key_length &&
            !memcmp(contents + offset, source->key, key_length) &&
            contents[offset + key_length] == '=') {
            return copy_ascii(value, capacity,
                              contents + offset + key_length + 1u,
                              end - offset - key_length - 1u);
        }
        offset = end + 1u;
    }
    return 0;
}

static void read_value(const struct value_source *source, char *value,
                       size_t capacity)
{
    value[0] = '\0';
    if (source->kind == VALUE_LITERAL) {
        copy_ascii(value, capacity, source->value,
                   string_length(source->value));
    } else if (source->kind == VALUE_FILE) {
        (void)read_file(source->value, value, capacity);
    } else {
        (void)read_field(source, value, capacity);
    }
}

static void refresh_item(struct custom_item *item)
{
    char value[VALUE_CAPACITY] = {0};
    read_value(&item->source, value, sizeof(value));
    if (!value[0]) copy_ascii(value, sizeof(value), "Unavailable", 11u);
    __atomic_add_fetch(&item->cache_generation, 1u, __ATOMIC_SEQ_CST);
    memcpy(item->cached_value, value, sizeof(value));
    __atomic_add_fetch(&item->cache_generation, 1u, __ATOMIC_SEQ_CST);
}

static void cached_value(const struct custom_item *item, char *value)
{
    for (;;) {
        uint32_t before = __atomic_load_n(&item->cache_generation,
                                          __ATOMIC_SEQ_CST);
        if (before & 1u) continue;
        memcpy(value, item->cached_value, VALUE_CAPACITY);
        uint32_t after = __atomic_load_n(&item->cache_generation,
                                         __ATOMIC_SEQ_CST);
        if (before == after) return;
    }
}

static void *update_values(void *unused)
{
    (void)unused;
    while (__atomic_load_n(&updater_running, __ATOMIC_SEQ_CST)) {
        for (uint32_t index = 0; index < custom_count; index++)
            if (custom_items[index].source.kind != VALUE_LITERAL)
                refresh_item(&custom_items[index]);
        usleep(500000u);
    }
    return 0;
}

static struct custom_item *find_custom(struct utility_item *utility)
{
    for (uint32_t index = 0; index < custom_count; index++)
        if (custom_items[index].utility.label == utility->label)
            return &custom_items[index];
    return 0;
}

static void render_item(struct utility_item *utility, int *lines)
{
    struct custom_item *item = find_custom(utility);
    char value[VALUE_CAPACITY];
    uint16_t wide_value[VALUE_CAPACITY];
    set_line(lines[0], utility->label);
    if (!item) {
        set_line(lines[1], (const uint16_t[]){0});
        return;
    }
    cached_value(item, value);
    ascii_to_utf16(wide_value, VALUE_CAPACITY, value);
    *(uint8_t *)(lines[0] + 3) |= 0x20u;
    set_line(lines[1], wide_value);
}

static void render_heading(struct utility_item *utility, int *lines)
{
    set_line(lines[0], utility->label);
    set_line(lines[1], (const uint16_t[]){0});
}

static int string_is(const char *actual, const char *expected)
{
    size_t actual_length = string_length(actual);
    size_t expected_length = string_length(expected);
    return actual_length == expected_length &&
           !memcmp(actual, expected, actual_length);
}

static int parse_kind(const char *value, size_t length, enum value_kind *kind)
{
    if (length == 7u && !memcmp(value, "literal", 7u)) {
        *kind = VALUE_LITERAL;
        return 1;
    }
    if (length == 4u && !memcmp(value, "file", 4u)) {
        *kind = VALUE_FILE;
        return 1;
    }
    if (length == 5u && !memcmp(value, "field", 5u)) {
        *kind = VALUE_FIELD;
        return 1;
    }
    return 0;
}

static int next_column(const char **cursor, const char *end,
                       const char **value, size_t *length)
{
    const char *separator = *cursor;
    while (separator < end && *separator != '|') separator++;
    *value = *cursor;
    *length = (size_t)(separator - *cursor);
    *cursor = separator < end ? separator + 1 : separator;
    return separator < end;
}

static void parse_definitions(void)
{
    const char *specifications = getenv("RX3_MENU_ITEMS");
    if (!specifications) return;
    const char *line = specifications;
    while (*line && definition_count < MENU_CAPACITY - STOCK_COUNT) {
        const char *end = line;
        while (*end && *end != '\n') end++;
        const char *cursor = line;
        const char *section, *label, *kind, *value, *key;
        size_t section_length, label_length, kind_length, value_length;
        if (!next_column(&cursor, end, &section, &section_length) ||
            !next_column(&cursor, end, &label, &label_length) ||
            !next_column(&cursor, end, &kind, &kind_length) ||
            !next_column(&cursor, end, &value, &value_length)) {
            line = *end ? end + 1 : end;
            continue;
        }
        key = cursor;
        size_t key_length = (size_t)(end - cursor);
        struct definition *definition = &definitions[definition_count];
        if (!section_length || !label_length || !value_length ||
            !parse_kind(kind, kind_length, &definition->source.kind)) {
            line = *end ? end + 1 : end;
            continue;
        }
        copy_ascii(definition->section, sizeof(definition->section), section,
                   section_length);
        copy_ascii(definition->label, sizeof(definition->label), label,
                   label_length);
        copy_ascii(definition->source.value, sizeof(definition->source.value),
                   value, value_length);
        copy_ascii(definition->source.key, sizeof(definition->source.key), key,
                   key_length);
        if (definition->source.kind != VALUE_FIELD || key_length)
            definition_count++;
        line = *end ? end + 1 : end;
    }
    if (*line) overflow = 1;
}

static int append_utility(const struct utility_item *item)
{
    if (registry.count >= MENU_CAPACITY) {
        overflow = 1;
        return 0;
    }
    registry.items[registry.count++] = *item;
    return 1;
}

static int append_custom(const char *label, const struct value_source *source,
                         const struct utility_item *prototype)
{
    if (custom_count >= MENU_CAPACITY - STOCK_COUNT) {
        overflow = 1;
        return 0;
    }
    struct custom_item *item = &custom_items[custom_count++];
    item->utility = *prototype;
    /* Stock Utility labels use two leading spaces for section children. */
    item->label[0] = ' ';
    item->label[1] = ' ';
    ascii_to_utf16(item->label + 2u, TEXT_CAPACITY - 2u, label);
    item->utility.label = item->label;
    item->utility.render = render_item;
    item->source = *source;
    refresh_item(item);
    if (source->kind != VALUE_LITERAL) dynamic_values = 1;
    return append_utility(&item->utility);
}

static int append_heading(const char *label,
                          const struct utility_item *prototype)
{
    if (heading_count >= MENU_CAPACITY - STOCK_COUNT) {
        overflow = 1;
        return 0;
    }
    uint16_t *wide = heading_labels[heading_count++];
    ascii_to_utf16(wide, TEXT_CAPACITY, label);
    struct utility_item heading = *prototype;
    heading.label = wide;
    heading.render = render_heading;
    return append_utility(&heading);
}

static void append_section(const char *section,
                           const struct utility_item *prototype)
{
    for (uint32_t index = 0; index < definition_count; index++)
        if (string_is(definitions[index].section, section))
            (void)append_custom(definitions[index].label,
                                &definitions[index].source, prototype);
}

static int section_seen_before(uint32_t current)
{
    for (uint32_t index = 0; index < current; index++)
        if (string_is(definitions[index].section,
                      definitions[current].section)) return 1;
    return 0;
}

static void restore_literals(void)
{
    uint32_t stock_count = (uint32_t)STOCK_COUNT_ADDRESS;
    uint32_t stock_table = (uint32_t)STOCK_TABLE_ADDRESS;
    for (uint32_t index = 0; index < patched_counts; index++)
        (void)write_code(count_literals[index], &stock_count,
                         sizeof(stock_count));
    for (uint32_t index = 0; index < patched_tables; index++)
        (void)write_code(table_literals[index], &stock_table,
                         sizeof(stock_table));
    installed = 0;
    patched_counts = 0;
    patched_tables = 0;
}

static int literals_are_stock(void)
{
    for (uint32_t index = 0;
         index < sizeof(count_literals) / sizeof(count_literals[0]); index++)
        if (*(const uint32_t *)count_literals[index] !=
            (uint32_t)STOCK_COUNT_ADDRESS) return 0;
    for (uint32_t index = 0;
         index < sizeof(table_literals) / sizeof(table_literals[0]); index++)
        if (*(const uint32_t *)table_literals[index] !=
            (uint32_t)STOCK_TABLE_ADDRESS) return 0;
    return 1;
}

int rx3_menu_install(void)
{
    if (!literals_are_stock()) {
        log_line("menu extension unavailable: unexpected Utility literals");
        return 0;
    }
    const struct utility_item *stock =
        (const struct utility_item *)STOCK_TABLE_ADDRESS;
    const struct utility_item *readonly_prototype = &stock[32];
    const struct value_source version = {
        VALUE_LITERAL, RX3_TOOLKIT_VERSION, "",
    };
    parse_definitions();

    for (uint32_t index = 0; index <= 13u; index++)
        (void)append_utility(&stock[index]);
    append_section("deck", readonly_prototype);
    (void)append_utility(&stock[14]);
    for (uint32_t index = 15u; index <= 23u; index++)
        (void)append_utility(&stock[index]);
    append_section("mixer", readonly_prototype);
    (void)append_utility(&stock[24]);
    for (uint32_t index = 25u; index < STOCK_COUNT; index++)
        (void)append_utility(&stock[index]);
    append_section("general", readonly_prototype);

    (void)append_utility(&stock[24]);
    (void)append_heading("RX3-TOOLKIT", &stock[25]);
    (void)append_custom("VERSION", &version, readonly_prototype);
    append_section("RX3-TOOLKIT", readonly_prototype);

    for (uint32_t index = 0; index < definition_count; index++) {
        const char *section = definitions[index].section;
        if (string_is(section, "deck") || string_is(section, "mixer") ||
            string_is(section, "general") ||
            string_is(section, "RX3-TOOLKIT") ||
            section_seen_before(index))
            continue;
        if (!append_utility(&stock[24]) ||
            !append_heading(section, &stock[25])) break;
        append_section(section, readonly_prototype);
    }
    if (overflow) {
        log_line("menu extension rejected: too many Utility rows");
        return 0;
    }

    uint32_t count_address = (uint32_t)(unsigned long)&registry.count;
    uint32_t table_address = (uint32_t)(unsigned long)&registry.items[0];
    for (uint32_t index = 0;
         index < sizeof(count_literals) / sizeof(count_literals[0]); index++) {
        patched_counts = index + 1u;
        if (write_code(count_literals[index], &count_address,
                       sizeof(count_address))) {
            restore_literals();
            log_line("menu extension rejected: Utility count redirect failed");
            return 0;
        }
    }
    for (uint32_t index = 0;
         index < sizeof(table_literals) / sizeof(table_literals[0]); index++) {
        patched_tables = index + 1u;
        if (write_code(table_literals[index], &table_address,
                       sizeof(table_address))) {
            restore_literals();
            log_line("menu extension rejected: Utility table redirect failed");
            return 0;
        }
    }
    installed = 1;
    if (dynamic_values) {
        updater_running = 1;
        if (!pthread_create(&updater_thread, 0, update_values, 0)) {
            updater_started = 1;
        } else {
            updater_running = 0;
            log_line("warning: Utility value updater could not start");
        }
    }
    rx3_log_number("read-only Utility menu rows installed = ",
                   registry.count - STOCK_COUNT);
    return 1;
}

void rx3_menu_remove(void)
{
    __atomic_store_n(&updater_running, 0, __ATOMIC_SEQ_CST);
    if (updater_started) {
        pthread_join(updater_thread, 0);
        updater_started = 0;
    }
    if (installed) restore_literals();
}
