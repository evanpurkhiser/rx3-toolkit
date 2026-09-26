// SPDX-License-Identifier: MPL-2.0
/* Volatile XDJ-RX3 1.19 crossfader table replacement. */

typedef unsigned int size_t;
typedef int ssize_t;
typedef long off_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

extern int open(const char *, int, ...);
extern ssize_t read(int, void *, size_t);
extern ssize_t write(int, const void *, size_t);
extern int close(int);
extern int mprotect(void *, size_t, int);
extern long sysconf(int);
extern void *memcpy(void *, const void *, size_t);

#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 0100
#define O_TRUNC 01000
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define _SC_PAGESIZE 30

#define CONFIG_PATH "/root/pdj/rx3-crossfader.config"
#define READY_PATH "/tmp/rx3-crossfader-curve.ready"
#define LOG_PATH "/tmp/rx3-crossfader-curve.log"
#define TABLE_ENTRIES 1024u
#define TABLE_BYTES (TABLE_ENTRIES * sizeof(uint16_t))
#define FULL_SCALE 32767u
#define MAX_POINTS 32u

struct curve_point {
    double position;
    double gain;
};

struct curve_config {
    const char *target;
    unsigned long address;
    uint32_t stock_hash;
    unsigned int point_count;
    struct curve_point points[MAX_POINTS];
};

static size_t string_length(const char *text)
{
    size_t length = 0;
    while (text[length])
        length++;
    return length;
}

static void log_line(const char *text)
{
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return;
    (void)write(fd, text, string_length(text));
    (void)write(fd, "\n", 1u);
    close(fd);
}

static void publish_ready(const char *target)
{
    int fd = open(READY_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return;
    (void)write(fd, target, string_length(target));
    (void)write(fd, "\n", 1u);
    close(fd);
}

static int same_string(const char *left, const char *right)
{
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static int parse_unsigned(const char *text, size_t length, size_t *cursor,
                          unsigned int *value)
{
    unsigned int parsed = 0;
    if (*cursor >= length || text[*cursor] < '0' || text[*cursor] > '9')
        return 0;
    while (*cursor < length && text[*cursor] >= '0' && text[*cursor] <= '9') {
        parsed = parsed * 10u + (unsigned int)(text[*cursor] - '0');
        (*cursor)++;
    }
    *value = parsed;
    return 1;
}

static int parse_unit_decimal(const char *text, size_t length, size_t *cursor,
                              double *value)
{
    unsigned int whole;
    double fraction = 0.0;
    double place = 0.1;
    if (!parse_unsigned(text, length, cursor, &whole) || whole > 1u)
        return 0;
    if (*cursor < length && text[*cursor] == '.') {
        (*cursor)++;
        if (*cursor >= length || text[*cursor] < '0' || text[*cursor] > '9')
            return 0;
        while (*cursor < length && text[*cursor] >= '0' && text[*cursor] <= '9') {
            fraction += (double)(text[*cursor] - '0') * place;
            place *= 0.1;
            (*cursor)++;
        }
    }
    if (whole == 1u && fraction != 0.0)
        return 0;
    *value = (double)whole + fraction;
    return 1;
}

static int parse_config_text(const char *text, size_t length,
                             struct curve_config *config)
{
    char target[16];
    size_t cursor = 0;
    size_t target_length = 0;
    unsigned int point_index;

    while (cursor < length && text[cursor] != ' ') {
        if (target_length + 1u >= sizeof(target)) return 0;
        target[target_length++] = text[cursor++];
    }
    target[target_length] = '\0';
    if (cursor == length || text[cursor++] != ' ' ||
        !parse_unsigned(text, length, &cursor, &config->point_count) ||
        config->point_count < 2u || config->point_count > MAX_POINTS ||
        cursor == length || text[cursor++] != '\n')
        return 0;

    for (point_index = 0; point_index < config->point_count; point_index++) {
        struct curve_point *point = &config->points[point_index];
        if (!parse_unit_decimal(text, length, &cursor, &point->position) ||
            cursor == length || text[cursor++] != ' ' ||
            !parse_unit_decimal(text, length, &cursor, &point->gain) ||
            cursor == length || text[cursor++] != '\n')
            return 0;
        if (point_index > 0u &&
            point->position <= config->points[point_index - 1u].position)
            return 0;
        if (point_index > 0u &&
            point->gain < config->points[point_index - 1u].gain)
            return 0;
    }
    if (cursor != length || config->points[0].position != 0.0 ||
        config->points[0].gain != 0.0 ||
        config->points[config->point_count - 1u].position != 1.0 ||
        config->points[config->point_count - 1u].gain != 1.0)
        return 0;

    if (same_string(target, "mid")) {
        config->target = "mid";
        config->address = 0x00421aa0ul;
        config->stock_hash = 0xe61c0a56u;
    } else if (same_string(target, "sharp")) {
        config->target = "sharp";
        config->address = 0x004222a0ul;
        config->stock_hash = 0x99bbfe6fu;
    } else if (same_string(target, "mid-half")) {
        config->target = "mid-half";
        config->address = 0x00422aa0ul;
        config->stock_hash = 0xb70ffe4au;
    } else {
        return 0;
    }
    return 1;
}

static uint32_t table_hash(const void *bytes)
{
    const uint8_t *cursor = bytes;
    uint32_t hash = 2166136261u;
    unsigned int index;
    for (index = 0; index < TABLE_BYTES; index++) {
        hash ^= cursor[index];
        hash *= 16777619u;
    }
    return hash;
}

static void generate_table(uint16_t table[TABLE_ENTRIES],
                           const struct curve_config *config)
{
    unsigned int index;
    unsigned int segment = config->point_count - 2u;
    for (index = 0; index < TABLE_ENTRIES; index++) {
        double position = 1.0 - (double)index / 1023.0;
        const struct curve_point *left;
        const struct curve_point *right;
        double amount;
        double gain;
        while (segment > 0u && position < config->points[segment].position)
            segment--;
        left = &config->points[segment];
        right = &config->points[segment + 1u];
        amount = (position - left->position) /
                 (right->position - left->position);
        gain = left->gain + amount * (right->gain - left->gain);
        unsigned int sample = (unsigned int)(gain * (double)FULL_SCALE + 0.5);
        if (index > 0u && sample > table[index - 1u])
            sample = table[index - 1u];
        table[index] = (uint16_t)sample;
    }
}

static int replace_table(const struct curve_config *config)
{
    uint16_t generated[TABLE_ENTRIES];
    long page_size = sysconf(_SC_PAGESIZE);
    unsigned long mask;
    unsigned long first;
    unsigned long last;
    size_t span;

    if (table_hash((const void *)config->address) != config->stock_hash)
        return 0;
    generate_table(generated, config);
    if (page_size <= 0)
        page_size = 4096;
    mask = (unsigned long)page_size - 1u;
    first = config->address & ~mask;
    last = (config->address + TABLE_BYTES - 1u) & ~mask;
    span = (size_t)(last - first) + (size_t)page_size;
    if (mprotect((void *)first, span, PROT_READ | PROT_WRITE))
        return 0;
    memcpy((void *)config->address, generated, TABLE_BYTES);
    if (mprotect((void *)first, span, PROT_READ | PROT_EXEC))
        return 0;
    return 1;
}

__attribute__((constructor)) static void crossfader_curve_init(void)
{
    char contents[2048];
    struct curve_config config;
    int fd = open(CONFIG_PATH, O_RDONLY);
    ssize_t count;

    if (fd < 0) {
        log_line("crossfader curve: configuration is missing");
        return;
    }
    count = read(fd, contents, sizeof(contents));
    close(fd);
    if (count <= 0 || count == (ssize_t)sizeof(contents) ||
        !parse_config_text(contents, (size_t)count, &config)) {
        log_line("crossfader curve: configuration rejected");
        return;
    }
    if (!replace_table(&config)) {
        log_line("crossfader curve: stock table guard failed");
        return;
    }
    log_line("crossfader curve: selected table replaced in memory");
    publish_ready(config.target);
}
