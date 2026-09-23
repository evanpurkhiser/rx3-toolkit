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

struct curve_config {
    const char *target;
    unsigned long address;
    uint32_t stock_hash;
    double midpoint_db;
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

static int parse_config_text(const char *text, size_t length,
                             struct curve_config *config)
{
    char target[16];
    size_t cursor = 0;
    size_t target_length = 0;
    unsigned int whole = 0;
    double fraction = 0.0;
    double place = 0.1;

    while (cursor < length && text[cursor] != ' ') {
        if (target_length + 1u >= sizeof(target)) return 0;
        target[target_length++] = text[cursor++];
    }
    target[target_length] = '\0';
    if (cursor == length || text[cursor++] != ' ' || cursor == length ||
        text[cursor++] != '-') return 0;
    if (cursor == length || text[cursor] < '0' || text[cursor] > '9') return 0;
    while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9')
        whole = whole * 10u + (unsigned int)(text[cursor++] - '0');
    if (cursor < length && text[cursor] == '.') {
        cursor++;
        if (cursor == length || text[cursor] < '0' || text[cursor] > '9') return 0;
        while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            fraction += (double)(text[cursor++] - '0') * place;
            place *= 0.1;
        }
    }
    if (cursor < length && text[cursor] == '\n') cursor++;
    if (cursor != length) return 0;
    config->midpoint_db = -((double)whole + fraction);
    if (config->midpoint_db < -48.0 || config->midpoint_db > -3.0103) return 0;

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

/* Small self-contained log/exp implementation; the preload cannot assume that
 * libm's public symbols are available in the vendor process. */
static double natural_log(double value)
{
    const double ln2 = 0.6931471805599453;
    double z;
    double square;
    double term;
    double sum;
    int scale = 0;
    int divisor;

    while (value < 0.75) {
        value *= 2.0;
        scale--;
    }
    while (value > 1.5) {
        value *= 0.5;
        scale++;
    }
    z = (value - 1.0) / (value + 1.0);
    square = z * z;
    term = z;
    sum = z;
    for (divisor = 3; divisor <= 21; divisor += 2) {
        term *= square;
        sum += term / (double)divisor;
    }
    return 2.0 * sum + (double)scale * ln2;
}

static double natural_exp(double value)
{
    const double ln2 = 0.6931471805599453;
    double term = 1.0;
    double sum = 1.0;
    int scale = 0;
    int index;

    while (value < -0.35) {
        value += ln2;
        scale--;
    }
    while (value > 0.35) {
        value -= ln2;
        scale++;
    }
    for (index = 1; index <= 14; index++) {
        term *= value / (double)index;
        sum += term;
    }
    while (scale < 0) {
        sum *= 0.5;
        scale++;
    }
    while (scale > 0) {
        sum *= 2.0;
        scale--;
    }
    return sum;
}

static void generate_table(uint16_t table[TABLE_ENTRIES], double midpoint_db)
{
    const double exponent = midpoint_db / -6.020599913279624;
    unsigned int index;
    table[0] = FULL_SCALE;
    for (index = 1; index + 1u < TABLE_ENTRIES; index++) {
        double position = 1.0 - (double)index / 1023.0;
        double gain = natural_exp(exponent * natural_log(position));
        unsigned int sample = (unsigned int)(gain * (double)FULL_SCALE + 0.5);
        if (sample > table[index - 1u])
            sample = table[index - 1u];
        table[index] = (uint16_t)sample;
    }
    table[TABLE_ENTRIES - 1u] = 0u;
}

static int table_power_is_safe(const uint16_t table[TABLE_ENTRIES])
{
    const unsigned long long limit =
        (unsigned long long)FULL_SCALE * FULL_SCALE + 2u * FULL_SCALE;
    unsigned int index;
    for (index = 0; index < TABLE_ENTRIES / 2u; index++) {
        unsigned long long left = table[index];
        unsigned long long right = table[TABLE_ENTRIES - 1u - index];
        if (left * left + right * right > limit)
            return 0;
    }
    return 1;
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
    generate_table(generated, config->midpoint_db);
    if (!table_power_is_safe(generated))
        return 0;
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
    char contents[512];
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
