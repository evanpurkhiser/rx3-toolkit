// SPDX-License-Identifier: MPL-2.0
/* Dirty-tile framebuffer stream for the XDJ-RX3.
 *
 * One detached worker owns /dev/fb0 and the TCP listener. It sends a complete
 * keyframe to each new client, then scans 32x32 tiles for changes. Socket
 * writes have a one-second deadline, so display or audio work in rbp can never
 * wait for a receiver.
 */

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned int size_t;
typedef int ssize_t;
typedef long off_t;
typedef unsigned long pthread_t;

struct timespec {
    long tv_sec;
    long tv_nsec;
};

struct sockaddr {
    uint16_t family;
    char data[14];
};

struct sockaddr_in {
    uint16_t family;
    uint16_t port;
    uint32_t address;
    uint8_t zero[8];
};

struct pollfd {
    int fd;
    short events;
    short revents;
};

struct fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
};

struct fb_fix_screeninfo {
    char id[16];
    unsigned long smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    unsigned long mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
};

extern int open(const char *, int, ...);
extern int close(int);
extern int ioctl(int, unsigned long, ...);
extern void *mmap(void *, size_t, int, int, int, off_t);
extern int munmap(void *, size_t);
extern void *malloc(size_t);
extern void free(void *);
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern int memcmp(const void *, const void *, size_t);
extern char *getenv(const char *);
extern long strtol(const char *, char **, int);
extern int socket(int, int, int);
extern int setsockopt(int, int, int, const void *, unsigned int);
extern int bind(int, const struct sockaddr *, unsigned int);
extern int listen(int, int);
extern int accept(int, struct sockaddr *, unsigned int *);
extern ssize_t send(int, const void *, size_t, int);
extern int poll(struct pollfd *, unsigned long, int);
extern int nanosleep(const struct timespec *, struct timespec *);
extern int clock_gettime(int, struct timespec *);
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_detach(pthread_t);
extern ssize_t write(int, const void *, size_t);
extern ssize_t readlink(const char *, char *, size_t);
extern int compress2(uint8_t *, unsigned long *, const uint8_t *,
                     unsigned long, int) __attribute__((weak));

#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_APPEND 02000
#define PROT_READ 1
#define MAP_SHARED 1
#define MAP_FAILED ((void *)-1)
#define AF_INET 2
#define SOCK_STREAM 1
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020
#define CLOCK_MONOTONIC 1
#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

#define STREAM_ADDRESS 0x0264fea9u /* 169.254.100.2 in network byte order */
#define DEFAULT_PORT 7351u
#define DEFAULT_FPS 30u
#define MIN_FPS 1u
#define MAX_FPS 30u
#define TILE_SIZE 32u
#define PIXEL_RGB565_LE 1u
#define SEND_DEADLINE_MS 1000u
#define READY_PATH "/tmp/rx3-framebuffer-stream.ready"
#define LOG_PATH "/tmp/rx3-framebuffer-stream.log"

#define TYPE_HELLO 1u
#define TYPE_FRAME 2u
#define FLAG_KEYFRAME 1u
#define WIRE_HEADER_SIZE 16u
#define HELLO_PAYLOAD_SIZE 8u
#define FRAME_PREFIX_SIZE 12u
#define RECT_PREFIX_SIZE 12u
#define RECT_CODEC_XOR_RLE 0x40000000u
#define RECT_CODEC_XOR_RLE_LZ4 0x80000000u
#define RECT_CODEC_XOR_RLE_ZLIB 0xc0000000u
#define RECT_LENGTH_MASK 0x3fffffffu
#define LZ4_HASH_LOG 14u
#define LZ4_HASH_SIZE (1u << LZ4_HASH_LOG)
#define LZ4_INVALID_POSITION 0xffffffffu
#define MAX_MESSAGE_SIZE (16u * 1024u * 1024u)

static uint32_t wire_sequence;

/* The runtime builder intentionally links without libc at build time. Clang
 * may lower equality-only memcmp calls to bcmp at -O2, while the RX3's libc
 * does not export bcmp. Keep the lowered calls inside this library. */
int bcmp(const void *left, const void *right, size_t length)
{
    const uint8_t *left_bytes = left;
    const uint8_t *right_bytes = right;
    while (length--) {
        if (*left_bytes++ != *right_bytes++)
            return 1;
    }
    return 0;
}

static void put16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)(value >> 8u);
    target[1] = (uint8_t)value;
}

static void put32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24u);
    target[1] = (uint8_t)(value >> 16u);
    target[2] = (uint8_t)(value >> 8u);
    target[3] = (uint8_t)value;
}

static void put64(uint8_t *target, uint64_t value)
{
    put32(target, (uint32_t)(value >> 32u));
    put32(target + 4u, (uint32_t)value);
}

static size_t string_length(const char *text)
{
    size_t length = 0;
    while (text[length])
        length++;
    return length;
}

static void log_line(const char *text)
{
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return;
    (void)write(fd, text, string_length(text));
    (void)write(fd, "\n", 1u);
    close(fd);
}

static void publish_ready(void)
{
    int fd = open(READY_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return;
    (void)write(fd, "ready\n", 6u);
    close(fd);
}

static int running_in_rbp(void)
{
    static const char expected[] = "/root/pdj/rbp";
    char executable[sizeof(expected)];
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable));

    return length == (ssize_t)(sizeof(expected) - 1u) &&
           !memcmp(executable, expected, sizeof(expected) - 1u);
}

static unsigned int configuration(const char *name, unsigned int fallback,
                                  unsigned int minimum, unsigned int maximum)
{
    const char *text = getenv(name);
    if (!text || !text[0])
        return fallback;

    char *end = 0;
    long value = strtol(text, &end, 10);
    if (!end || *end || value < (long)minimum || value > (long)maximum)
        return fallback;
    return (unsigned int)value;
}

static uint32_t monotonic_milliseconds(void);

static int send_bytes(int client, const void *buffer, size_t length)
{
    const uint8_t *cursor = buffer;
    uint32_t deadline = monotonic_milliseconds() + SEND_DEADLINE_MS;
    while (length) {
        ssize_t sent = send(client, cursor, length, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent > 0) {
            cursor += sent;
            length -= (size_t)sent;
            continue;
        }

        uint32_t now = monotonic_milliseconds();
        int remaining = (int)(deadline - now);
        if (remaining <= 0 || remaining > (int)SEND_DEADLINE_MS)
            return 0;
        struct pollfd ready = {client, POLLOUT, 0};
        if (poll(&ready, 1u, remaining) <= 0 ||
            (ready.revents & (POLLERR | POLLHUP | POLLNVAL)))
            return 0;
    }
    return 1;
}

static int send_packet(int client, uint8_t type, uint16_t flags,
                       const void *payload, uint32_t payload_length)
{
    uint8_t header[WIRE_HEADER_SIZE] = {'R', 'X', '3', 'F', 1u, type};
    put16(header + 6u, flags);
    put32(header + 8u, ++wire_sequence);
    put32(header + 12u, payload_length);

    return send_bytes(client, header, sizeof(header)) &&
           (!payload_length || send_bytes(client, payload, payload_length));
}

static uint64_t monotonic_nanoseconds(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time))
        return 0;
    return (uint64_t)(unsigned long)time.tv_sec * 1000000000ull +
           (uint64_t)(unsigned long)time.tv_nsec;
}

static uint32_t monotonic_milliseconds(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time))
        return 0u;
    return (uint32_t)time.tv_sec * 1000u +
           (uint32_t)time.tv_nsec / 1000000u;
}

static void copy_tile(uint8_t *target, const uint8_t *framebuffer,
                      uint8_t *shadow, uint32_t stride,
                      uint32_t bytes_per_pixel, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height)
{
    size_t row_bytes = (size_t)width * bytes_per_pixel;
    size_t offset = (size_t)y * stride + (size_t)x * bytes_per_pixel;
    for (uint32_t row = 0; row < height; row++) {
        memcpy(target, framebuffer + offset, row_bytes);
        memcpy(shadow + offset, framebuffer + offset, row_bytes);
        target += row_bytes;
        offset += stride;
    }
}

/* Encode changed RGB565 pixels as alternating skip and XOR-literal runs.
 * Each control byte holds a 1..128 pixel run. Bit 7 selects XOR literals;
 * clear controls skip pixels already present in the receiver's previous
 * frame. Dense changes fall back to raw RGB565 before the codec expands. */
static uint32_t encode_delta_tile(uint8_t *target,
                                  const uint8_t *framebuffer, uint8_t *shadow,
                                  uint32_t stride, uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height)
{
    uint32_t raw_length = width * height * 2u;
    uint32_t encoded_length = 0u;
    int changed = 0;
    int use_raw = 0;

    for (uint32_t row = 0; row < height; row++) {
        size_t offset = (size_t)(y + row) * stride + (size_t)x * 2u;
        const uint16_t *source = (const uint16_t *)(framebuffer + offset);
        uint16_t *previous = (uint16_t *)(shadow + offset);
        uint32_t column = 0u;

        while (column < width) {
            if (source[column] == previous[column]) {
                uint32_t run = 1u;
                while (run < 128u && column + run < width &&
                       source[column + run] == previous[column + run])
                    run++;
                if (!use_raw) {
                    if (encoded_length + 1u >= raw_length) {
                        use_raw = 1;
                    } else {
                        target[encoded_length++] = (uint8_t)(run - 1u);
                    }
                }
                column += run;
                continue;
            }

            uint32_t run = 1u;
            while (run < 128u && column + run < width &&
                   source[column + run] != previous[column + run])
                run++;
            changed = 1;
            if (!use_raw && encoded_length + 1u + run * 2u >= raw_length)
                use_raw = 1;
            if (!use_raw)
                target[encoded_length++] = (uint8_t)(0x80u | (run - 1u));
            for (uint32_t index = 0; index < run; index++) {
                uint16_t value = source[column + index];
                uint16_t delta = (uint16_t)(value ^ previous[column + index]);
                previous[column + index] = value;
                if (!use_raw) {
                    target[encoded_length++] = (uint8_t)delta;
                    target[encoded_length++] = (uint8_t)(delta >> 8u);
                }
            }
            column += run;
        }
    }

    if (!changed)
        return 0u;
    if (use_raw || encoded_length >= raw_length) {
        copy_tile(target, framebuffer, shadow, stride, 2u,
                  x, y, width, height);
        return raw_length;
    }
    return RECT_CODEC_XOR_RLE | encoded_length;
}

/* Build the same skip/literal delta over the full visible frame. Runs stop at
 * row boundaries, which keeps framebuffer reads linear without division in
 * the hot loop. The result is then an excellent zlib input: real RX3 frames
 * shrink from roughly 500 KiB of dirty tiles to 27 KiB at level 1. */
static int build_frame_delta(uint8_t *target, uint32_t capacity,
                             const uint8_t *framebuffer,
                             const uint8_t *shadow, uint32_t stride,
                             uint32_t width, uint32_t height,
                             uint32_t *result_length)
{
    uint32_t length = 0u;
    int changed = 0;
    for (uint32_t row = 0; row < height; row++) {
        size_t offset = (size_t)row * stride;
        const uint16_t *source = (const uint16_t *)(framebuffer + offset);
        const uint16_t *previous = (const uint16_t *)(shadow + offset);
        uint32_t column = 0u;
        while (column < width) {
            if (source[column] == previous[column]) {
                uint32_t run = 1u;
                while (run < 128u && column + run < width &&
                       source[column + run] == previous[column + run])
                    run++;
                if (length >= capacity)
                    return -1;
                target[length++] = (uint8_t)(run - 1u);
                column += run;
                continue;
            }

            uint32_t run = 1u;
            while (run < 128u && column + run < width &&
                   source[column + run] != previous[column + run])
                run++;
            if (length + 1u + run * 2u > capacity)
                return -1;
            target[length++] = (uint8_t)(0x80u | (run - 1u));
            for (uint32_t index = 0; index < run; index++) {
                uint16_t delta = (uint16_t)(source[column + index] ^
                                            previous[column + index]);
                target[length++] = (uint8_t)delta;
                target[length++] = (uint8_t)(delta >> 8u);
            }
            changed = 1;
            column += run;
        }
    }
    *result_length = length;
    return changed;
}

static void update_shadow(uint8_t *shadow, const uint8_t *framebuffer,
                          uint32_t stride, uint32_t width, uint32_t height)
{
    size_t row_bytes = (size_t)width * 2u;
    for (uint32_t row = 0; row < height; row++) {
        size_t offset = (size_t)row * stride;
        memcpy(shadow + offset, framebuffer + offset, row_bytes);
    }
}

static uint32_t read32le(const uint8_t *source)
{
    return (uint32_t)source[0] | (uint32_t)source[1] << 8u |
           (uint32_t)source[2] << 16u | (uint32_t)source[3] << 24u;
}

static int lz4_write_length(uint8_t **output, const uint8_t *end,
                            uint32_t length)
{
    while (length >= 255u) {
        if (*output >= end)
            return 0;
        *(*output)++ = 255u;
        length -= 255u;
    }
    if (*output >= end)
        return 0;
    *(*output)++ = (uint8_t)length;
    return 1;
}

/* A compact encoder for the standard LZ4 block format. It deliberately uses
 * one hash probe per input position: the XOR-RLE stream is highly repetitive,
 * and favoring predictable CPU cost matters more than optimal parsing here. */
static uint32_t lz4_compress_block(const uint8_t *input, uint32_t input_length,
                                   uint8_t *output, uint32_t output_capacity,
                                   uint32_t *hash_table)
{
    uint8_t *cursor = output;
    const uint8_t *output_end = output + output_capacity;
    uint32_t anchor = 0u;
    uint32_t position = 0u;
    memset(hash_table, 0xff, LZ4_HASH_SIZE * sizeof(*hash_table));

    /* LZ4's conventional end conditions leave five final literals and avoid
     * starting a match in the last twelve bytes. */
    while (position + 12u <= input_length) {
        uint32_t sequence = read32le(input + position);
        uint32_t hash = (sequence * 2654435761u) >> (32u - LZ4_HASH_LOG);
        uint32_t reference = hash_table[hash];
        hash_table[hash] = position;
        if (reference == LZ4_INVALID_POSITION || position - reference > 65535u ||
            read32le(input + reference) != sequence) {
            position++;
            continue;
        }

        uint32_t match_length = 4u;
        uint32_t match_limit = input_length - 5u;
        while (position + match_length < match_limit &&
               input[reference + match_length] == input[position + match_length])
            match_length++;

        uint32_t literal_length = position - anchor;
        if (cursor >= output_end)
            return 0u;
        uint8_t *token = cursor++;
        *token = (uint8_t)((literal_length < 15u ? literal_length : 15u) << 4u);
        if (literal_length >= 15u &&
            !lz4_write_length(&cursor, output_end, literal_length - 15u))
            return 0u;
        if ((uint32_t)(output_end - cursor) < literal_length + 2u)
            return 0u;
        memcpy(cursor, input + anchor, literal_length);
        cursor += literal_length;
        uint32_t offset = position - reference;
        *cursor++ = (uint8_t)offset;
        *cursor++ = (uint8_t)(offset >> 8u);

        uint32_t match_code = match_length - 4u;
        *token |= (uint8_t)(match_code < 15u ? match_code : 15u);
        if (match_code >= 15u &&
            !lz4_write_length(&cursor, output_end, match_code - 15u))
            return 0u;

        position += match_length;
        anchor = position;
        if (position >= 2u && position + 2u < input_length) {
            uint32_t insertion = position - 2u;
            uint32_t inserted = read32le(input + insertion);
            hash_table[(inserted * 2654435761u) >> (32u - LZ4_HASH_LOG)] = insertion;
        }
    }

    uint32_t literal_length = input_length - anchor;
    if (cursor >= output_end)
        return 0u;
    *cursor++ = (uint8_t)((literal_length < 15u ? literal_length : 15u) << 4u);
    if (literal_length >= 15u &&
        !lz4_write_length(&cursor, output_end, literal_length - 15u))
        return 0u;
    if ((uint32_t)(output_end - cursor) < literal_length)
        return 0u;
    memcpy(cursor, input + anchor, literal_length);
    cursor += literal_length;
    return (uint32_t)(cursor - output);
}

/* Return -1 when LZ4 cannot encode this update, 0 on a disconnected client,
 * and 1 after sending or when the frame is unchanged. */
static int send_lz4_frame(int client, const uint8_t *framebuffer,
                          uint8_t *shadow, uint8_t *message,
                          uint32_t message_capacity, uint8_t *scratch,
                          uint32_t *hash_table, uint32_t stride,
                          uint32_t width, uint32_t height)
{
    uint32_t raw_length = width * height * 2u;
    uint32_t delta_length = 0u;
    int changed = build_frame_delta(scratch, raw_length, framebuffer, shadow,
                                    stride, width, height, &delta_length);
    if (!changed)
        return changed < 0 ? -1 : 1;

    uint32_t prefix_length = FRAME_PREFIX_SIZE + RECT_PREFIX_SIZE;
    uint32_t compressed_length = lz4_compress_block(
        scratch, delta_length, message + prefix_length,
        message_capacity - prefix_length, hash_table);
    if (!compressed_length || compressed_length >= delta_length)
        return -1;

    put64(message, monotonic_nanoseconds());
    put16(message + 8u, 1u);
    put16(message + 10u, 0u);
    uint8_t *rectangle = message + FRAME_PREFIX_SIZE;
    put16(rectangle, 0u);
    put16(rectangle + 2u, 0u);
    put16(rectangle + 4u, (uint16_t)width);
    put16(rectangle + 6u, (uint16_t)height);
    put32(rectangle + 8u, RECT_CODEC_XOR_RLE_LZ4 | compressed_length);
    update_shadow(shadow, framebuffer, stride, width, height);
    return send_packet(client, TYPE_FRAME, 0u, message,
                       prefix_length + compressed_length);
}

/* Return -1 when zlib cannot encode this update, 0 on a disconnected client,
 * and 1 after sending or when the frame is unchanged. */
static int send_zlib_frame(int client, const uint8_t *framebuffer,
                           uint8_t *shadow, uint8_t *message,
                           uint32_t message_capacity, uint8_t *scratch,
                           uint32_t stride, uint32_t width, uint32_t height)
{
    if (!compress2)
        return -1;

    uint32_t raw_length = width * height * 2u;
    uint32_t delta_length = 0u;
    int changed = build_frame_delta(scratch, raw_length, framebuffer, shadow,
                                    stride, width, height, &delta_length);
    if (!changed)
        return changed < 0 ? -1 : 1;

    uint32_t prefix_length = FRAME_PREFIX_SIZE + RECT_PREFIX_SIZE;
    unsigned long compressed_length = message_capacity - prefix_length;
    if (compress2(message + prefix_length, &compressed_length,
                  scratch, delta_length, 1) ||
        compressed_length >= delta_length ||
        compressed_length > RECT_LENGTH_MASK)
        return -1;

    put64(message, monotonic_nanoseconds());
    put16(message + 8u, 1u);
    put16(message + 10u, 0u);
    uint8_t *rectangle = message + FRAME_PREFIX_SIZE;
    put16(rectangle, 0u);
    put16(rectangle + 2u, 0u);
    put16(rectangle + 4u, (uint16_t)width);
    put16(rectangle + 6u, (uint16_t)height);
    put32(rectangle + 8u, RECT_CODEC_XOR_RLE_ZLIB |
                             (uint32_t)compressed_length);
    update_shadow(shadow, framebuffer, stride, width, height);
    return send_packet(client, TYPE_FRAME, 0u, message,
                       prefix_length + (uint32_t)compressed_length);
}

static int send_frame(int client, const uint8_t *framebuffer, uint8_t *shadow,
                      uint8_t *message, uint32_t message_capacity,
                      uint32_t stride, uint32_t bytes_per_pixel,
                      uint32_t width, uint32_t height, int keyframe)
{
    put64(message, monotonic_nanoseconds());
    put16(message + 8u, 0u);
    put16(message + 10u, 0u);
    uint32_t message_length = FRAME_PREFIX_SIZE;
    uint16_t changed = 0u;
    for (uint32_t y = 0; y < height; y += TILE_SIZE) {
        uint32_t tile_height = height - y < TILE_SIZE ? height - y : TILE_SIZE;
        for (uint32_t x = 0; x < width; x += TILE_SIZE) {
            uint32_t tile_width = width - x < TILE_SIZE ? width - x : TILE_SIZE;
            uint32_t pixel_bytes = tile_width * tile_height * bytes_per_pixel;
            if (message_length + RECT_PREFIX_SIZE + pixel_bytes > message_capacity)
                return 0;
            uint8_t *tile = message + message_length;
            uint32_t encoded_length;
            if (keyframe) {
                copy_tile(tile + RECT_PREFIX_SIZE, framebuffer, shadow, stride,
                          bytes_per_pixel, x, y, tile_width, tile_height);
                encoded_length = pixel_bytes;
            } else {
                encoded_length = encode_delta_tile(
                    tile + RECT_PREFIX_SIZE, framebuffer, shadow, stride,
                    x, y, tile_width, tile_height);
                if (!encoded_length)
                    continue;
            }
            put16(tile, (uint16_t)x);
            put16(tile + 2u, (uint16_t)y);
            put16(tile + 4u, (uint16_t)tile_width);
            put16(tile + 6u, (uint16_t)tile_height);
            put32(tile + 8u, encoded_length);
            message_length += RECT_PREFIX_SIZE +
                              (encoded_length & RECT_LENGTH_MASK);
            changed++;
        }
    }

    if (!changed)
        return 1;
    put16(message + 8u, changed);
    uint16_t flags = keyframe ? FLAG_KEYFRAME : 0u;
    return send_packet(client, TYPE_FRAME, flags, message, message_length);
}

static int send_hello(int client, const struct fb_var_screeninfo *variable,
                      const struct fb_fix_screeninfo *fixed)
{
    uint8_t hello[HELLO_PAYLOAD_SIZE];
    put16(hello, (uint16_t)variable->xres);
    put16(hello + 2u, (uint16_t)variable->yres);
    put16(hello + 4u, (uint16_t)fixed->line_length);
    hello[6] = PIXEL_RGB565_LE;
    hello[7] = TILE_SIZE;
    return send_packet(client, TYPE_HELLO, 0u, hello, sizeof(hello));
}

static int open_listener(unsigned int port)
{
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        return -1;

    int enabled = 1;
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                     &enabled, sizeof(enabled));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.family = AF_INET;
    address.port = (uint16_t)((port << 8u) | (port >> 8u));
    address.address = STREAM_ADDRESS;
    if (bind(listener, (const struct sockaddr *)&address, sizeof(address)) ||
        listen(listener, 1)) {
        close(listener);
        return -1;
    }
    return listener;
}

static void stream_client(int client, const uint8_t *framebuffer,
                          uint8_t *shadow, uint8_t *message,
                          uint32_t message_capacity, uint8_t *scratch,
                          uint32_t *hash_table,
                          const struct fb_var_screeninfo *variable,
                          const struct fb_fix_screeninfo *fixed,
                          uint32_t visible_offset,
                          unsigned int frames_per_second)
{
    uint32_t bytes_per_pixel = (variable->bits_per_pixel + 7u) / 8u;
    const uint8_t *visible = framebuffer + visible_offset;
    memset(shadow, 0, fixed->smem_len);
    if (!send_hello(client, variable, fixed))
        return;

    struct timespec delay;
    delay.tv_sec = 0;
    delay.tv_nsec = (long)(1000000000u / frames_per_second);
    int keyframe = 1;
    for (;;) {
        int result;
        if (!keyframe) {
            result = send_lz4_frame(
                client, visible, shadow + visible_offset,
                message, message_capacity, scratch, hash_table,
                fixed->line_length, variable->xres, variable->yres);
            if (result < 0)
                result = send_zlib_frame(
                client, visible, shadow + visible_offset,
                message, message_capacity, scratch,
                fixed->line_length, variable->xres, variable->yres);
        } else {
            result = -1;
        }
        if (result < 0)
            result = send_frame(client, visible, shadow + visible_offset,
                                message, message_capacity,
                                fixed->line_length, bytes_per_pixel,
                                variable->xres, variable->yres, keyframe);
        if (!result)
            return;
        keyframe = 0;
        (void)nanosleep(&delay, 0);
    }
}

static void *stream_worker(void *unused)
{
    (void)unused;
    unsigned int frames_per_second = configuration(
        "RX3_FB_FPS", DEFAULT_FPS, MIN_FPS, MAX_FPS);
    unsigned int port = configuration("RX3_FB_PORT", DEFAULT_PORT, 1024u, 65535u);

    int framebuffer_fd = open("/dev/fb0", O_RDONLY);
    if (framebuffer_fd < 0) {
        log_line("rejected: could not open /dev/fb0");
        return 0;
    }
    struct fb_var_screeninfo variable;
    struct fb_fix_screeninfo fixed;
    memset(&variable, 0, sizeof(variable));
    memset(&fixed, 0, sizeof(fixed));
    if (ioctl(framebuffer_fd, FBIOGET_VSCREENINFO, &variable) ||
        ioctl(framebuffer_fd, FBIOGET_FSCREENINFO, &fixed)) {
        log_line("rejected: framebuffer geometry ioctl failed");
        close(framebuffer_fd);
        return 0;
    }

    uint32_t bytes_per_pixel = (variable.bits_per_pixel + 7u) / 8u;
    uint32_t visible_offset = variable.yoffset * fixed.line_length +
                              variable.xoffset * bytes_per_pixel;
    unsigned long visible_end = (unsigned long)visible_offset +
        (unsigned long)(variable.yres - 1u) * fixed.line_length +
        (unsigned long)variable.xres * bytes_per_pixel;
    if (!variable.xres || !variable.yres || !fixed.line_length ||
        !fixed.smem_len || bytes_per_pixel != 2u ||
        variable.xres > 4096u || variable.yres > 4096u ||
        fixed.line_length > 65535u ||
        variable.red.offset != 11u || variable.red.length != 5u ||
        variable.green.offset != 5u || variable.green.length != 6u ||
        variable.blue.offset != 0u || variable.blue.length != 5u ||
        visible_end > fixed.smem_len) {
        log_line("rejected: framebuffer is not addressable RGB565");
        close(framebuffer_fd);
        return 0;
    }

    uint8_t *framebuffer = mmap(0, fixed.smem_len, PROT_READ, MAP_SHARED,
                                framebuffer_fd, 0);
    close(framebuffer_fd);
    if (framebuffer == MAP_FAILED) {
        log_line("rejected: framebuffer mmap failed");
        return 0;
    }
    uint8_t *shadow = malloc(fixed.smem_len);
    if (!shadow) {
        log_line("rejected: framebuffer shadow allocation failed");
        munmap(framebuffer, fixed.smem_len);
        return 0;
    }
    uint32_t tile_columns = (variable.xres + TILE_SIZE - 1u) / TILE_SIZE;
    uint32_t tile_rows = (variable.yres + TILE_SIZE - 1u) / TILE_SIZE;
    uint32_t message_capacity = FRAME_PREFIX_SIZE +
        tile_columns * tile_rows * RECT_PREFIX_SIZE +
        variable.xres * variable.yres * bytes_per_pixel;
    if (message_capacity > MAX_MESSAGE_SIZE) {
        log_line("rejected: framebuffer frame exceeds protocol limit");
        free(shadow);
        munmap(framebuffer, fixed.smem_len);
        return 0;
    }
    uint8_t *message = malloc(message_capacity);
    if (!message) {
        log_line("rejected: framebuffer message allocation failed");
        free(shadow);
        munmap(framebuffer, fixed.smem_len);
        return 0;
    }
    uint32_t scratch_capacity = variable.xres * variable.yres * bytes_per_pixel;
    uint8_t *scratch = malloc(scratch_capacity);
    if (!scratch) {
        log_line("rejected: framebuffer codec allocation failed");
        free(message);
        free(shadow);
        munmap(framebuffer, fixed.smem_len);
        return 0;
    }
    uint32_t *hash_table = malloc(LZ4_HASH_SIZE * sizeof(*hash_table));
    if (!hash_table) {
        log_line("rejected: framebuffer LZ4 allocation failed");
        free(scratch);
        free(message);
        free(shadow);
        munmap(framebuffer, fixed.smem_len);
        return 0;
    }

    int listener = open_listener(port);
    if (listener < 0) {
        log_line("rejected: framebuffer TCP listener failed");
        free(hash_table);
        free(scratch);
        free(message);
        free(shadow);
        munmap(framebuffer, fixed.smem_len);
        return 0;
    }

    publish_ready();
    log_line("framebuffer stream active on 169.254.100.2:7351");
    for (;;) {
        int client = accept(listener, 0, 0);
        if (client < 0)
            continue;
        stream_client(client, framebuffer, shadow, message, message_capacity,
                      scratch, hash_table, &variable, &fixed,
                      visible_offset, frames_per_second);
        close(client);
    }
}

__attribute__((constructor)) static void initialize(void)
{
    if (!running_in_rbp())
        return;

    pthread_t thread;
    if (pthread_create(&thread, 0, stream_worker, 0)) {
        log_line("rejected: framebuffer stream worker could not start");
        return;
    }
    pthread_detach(thread);
}
