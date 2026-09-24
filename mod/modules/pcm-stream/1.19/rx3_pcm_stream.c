// SPDX-License-Identifier: MPL-2.0
/* Firmware 1.19 master-recorder PCM tap and bounded TCP stream.
 *
 * The audio callback is a single producer. It only validates the block, copies
 * it to a preallocated slot, publishes one index, and calls the stock method.
 * The worker converts floats to PCM16 and owns every socket operation. A full
 * ring drops the incoming block rather than delaying audio playback.
 */

typedef unsigned int size_t;
typedef int ssize_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long pthread_t;

struct sockaddr_in {
    uint16_t family;
    uint16_t port;
    uint32_t address;
    uint8_t zero[8];
};

struct timeval {
    long seconds;
    long microseconds;
};

struct timespec {
    long seconds;
    long nanoseconds;
};

extern int open(const char *, int, ...);
extern ssize_t write(int, const void *, size_t);
extern ssize_t readlink(const char *, char *, size_t);
extern int close(int);
extern void *mmap(void *, size_t, int, int, int, long);
extern int munmap(void *, size_t);
extern int mprotect(void *, size_t, int);
extern long sysconf(int);
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern int memcmp(const void *, const void *, size_t);
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_detach(pthread_t);
extern int socket(int, int, int);
extern int bind(int, const void *, unsigned int);
extern int listen(int, int);
extern int accept(int, void *, void *);
extern int setsockopt(int, int, int, const void *, unsigned int);
extern ssize_t send(int, const void *, size_t, int);
extern int fcntl(int, int, ...);
extern int nice(int);
extern int nanosleep(const struct timespec *, struct timespec *);

#define WAV_COPY_BUFFER ((unsigned long)0x0005bb48)
#define READY_PATH "/tmp/rx3-pcm-stream.ready"
#define LOG_PATH "/tmp/rx3-pcm-stream.log"

#define O_WRONLY 1
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_APPEND 02000
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void *)-1)
#define _SC_PAGESIZE 30
#define AF_INET 2
#define SOCK_STREAM 1
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_SNDTIMEO 21
#define MSG_NOSIGNAL 0x4000
#define F_GETFL 3
#define F_SETFL 4
#define O_NONBLOCK 04000

#define PCM_PORT 7355u
#define SAMPLE_RATE 44100u
#define CHANNELS 2u
#define MAX_BLOCK_FRAMES 4096u
#define RING_SLOTS 32u
#define RING_MASK (RING_SLOTS - 1u)

typedef struct {
    float left;
    float right;
} Float2;

struct pcm_slot {
    uint64_t first_frame;
    uint32_t frames;
    uint32_t dropped_frames;
    Float2 samples[MAX_BLOCK_FRAMES];
};

struct installed_hook {
    unsigned long address;
    uint8_t original[8];
    void *trampoline;
};

typedef int (*copy_buffer_fn)(void *, const Float2 *, int);

static const uint8_t copy_buffer_guard[8] = {
    0xf0, 0x40, 0x2d, 0xe9, 0x00, 0x40, 0xa0, 0xe1
};

static struct pcm_slot pcm_ring[RING_SLOTS];
static volatile uint32_t write_index;
static volatile uint32_t read_index;
static volatile uint32_t dropped_blocks;
static volatile uint32_t dropped_frames;
static uint64_t producer_frame;
static struct installed_hook copy_buffer_hook;
static copy_buffer_fn original_copy_buffer;

static void barrier(void)
{
    __sync_synchronize();
}

static uint16_t big16(uint16_t value)
{
    return (uint16_t)((value >> 8) | (value << 8));
}

static uint32_t big32(uint32_t value)
{
    return (value >> 24) | ((value >> 8) & 0x0000ff00u) |
           ((value << 8) & 0x00ff0000u) | (value << 24);
}

static void put_big64(uint8_t *output, uint64_t value)
{
    uint32_t high = big32((uint32_t)(value >> 32));
    uint32_t low = big32((uint32_t)value);
    memcpy(output, &high, sizeof(high));
    memcpy(output + sizeof(high), &low, sizeof(low));
}

static void log_line(const char *message)
{
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return;

    size_t length = 0;
    while (message[length])
        length++;
    (void)write(fd, message, length);
    (void)close(fd);
}

static int running_in_rbp(void)
{
    static const char expected[] = "/root/pdj/rbp";
    char executable[sizeof(expected)];
    ssize_t length = readlink("/proc/self/exe", executable,
                              sizeof(executable));

    return length == (ssize_t)(sizeof(expected) - 1u) &&
           !memcmp(executable, expected, sizeof(expected) - 1u);
}

static void clear_instruction_cache(unsigned long first, unsigned long last)
{
    register unsigned long r0 __asm__("r0") = first;
    register unsigned long r1 __asm__("r1") = last;
    register unsigned long r2 __asm__("r2") = 0;
    register unsigned long r7 __asm__("r7") = 0x0f0002u;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
}

static int write_code(unsigned long address, const void *bytes, size_t length)
{
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        page_size = 4096;
    unsigned long mask = (unsigned long)page_size - 1u;
    unsigned long first = address & ~mask;
    unsigned long last = (address + length - 1u) & ~mask;
    size_t span = (size_t)(last - first) + (size_t)page_size;

    if (mprotect((void *)first, span, PROT_READ | PROT_WRITE))
        return -1;
    memcpy((void *)address, bytes, length);
    clear_instruction_cache(address, address + length);
    return mprotect((void *)first, span, PROT_READ | PROT_EXEC);
}

static void *install_hook(struct installed_hook *hook, unsigned long address,
                          const uint8_t guard[8], void *replacement)
{
    uint32_t *trampoline;
    uint32_t patch[2];

    if (memcmp((const void *)address, guard, 8))
        return 0;

    trampoline = mmap(0, 4096, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED)
        return 0;
    memcpy(trampoline, (const void *)address, 8);
    trampoline[2] = 0xe51ff004u;
    trampoline[3] = (uint32_t)(address + 8u);
    clear_instruction_cache((unsigned long)trampoline,
                            (unsigned long)trampoline + 16u);
    if (mprotect(trampoline, 4096, PROT_READ | PROT_EXEC)) {
        (void)munmap(trampoline, 4096);
        return 0;
    }

    patch[0] = 0xe51ff004u;
    patch[1] = (uint32_t)(unsigned long)replacement;
    if (write_code(address, patch, sizeof(patch))) {
        (void)munmap(trampoline, 4096);
        return 0;
    }

    hook->address = address;
    memcpy(hook->original, guard, 8);
    hook->trampoline = trampoline;
    return trampoline;
}

static int hooked_copy_buffer(void *writer, const Float2 *samples, int frames)
{
    uint64_t first_frame = producer_frame;

    if (frames > 0)
        producer_frame += (uint32_t)frames;

    if (samples && frames > 0 && (uint32_t)frames <= MAX_BLOCK_FRAMES) {
        uint32_t write = write_index;
        uint32_t next = (write + 1u) & RING_MASK;

        if (next == read_index) {
            dropped_blocks++;
            dropped_frames += (uint32_t)frames;
        } else {
            struct pcm_slot *slot = &pcm_ring[write];
            slot->first_frame = first_frame;
            slot->frames = (uint32_t)frames;
            slot->dropped_frames = dropped_frames;
            memcpy(slot->samples, samples, (size_t)frames * sizeof(Float2));
            barrier();
            write_index = next;
        }
    } else if (frames > 0) {
        dropped_blocks++;
        dropped_frames += (uint32_t)frames;
    }

    return original_copy_buffer(writer, samples, frames);
}

static int send_all(int fd, const void *data, size_t length)
{
    const uint8_t *cursor = (const uint8_t *)data;

    while (length) {
        ssize_t sent = send(fd, cursor, length, MSG_NOSIGNAL);
        if (sent <= 0)
            return -1;
        cursor += sent;
        length -= (size_t)sent;
    }
    return 0;
}

static int send_message_header(int fd, uint8_t type, uint32_t sequence,
                               uint32_t payload_bytes, uint64_t first_frame,
                               uint32_t dropped)
{
    uint8_t header[28];
    uint32_t value32;

    memset(header, 0, sizeof(header));
    memcpy(header, "RX3A", 4);
    header[4] = 1;
    header[5] = type;
    value32 = big32(sequence);
    memcpy(header + 8, &value32, 4);
    value32 = big32(payload_bytes);
    memcpy(header + 12, &value32, 4);
    put_big64(header + 16, first_frame);
    value32 = big32(dropped);
    memcpy(header + 24, &value32, 4);
    return send_all(fd, header, sizeof(header));
}

static int send_config(int fd, uint32_t sequence, const struct pcm_slot *slot)
{
    uint8_t payload[12];
    uint32_t value32;
    uint16_t value16;

    value32 = big32(SAMPLE_RATE);
    memcpy(payload, &value32, 4);
    value16 = big16(CHANNELS);
    memcpy(payload + 4, &value16, 2);
    value16 = big16(1u);              /* signed PCM16LE */
    memcpy(payload + 6, &value16, 2);
    value32 = big32(slot->frames);
    memcpy(payload + 8, &value32, 4);

    if (send_message_header(fd, 1u, sequence, sizeof(payload),
                            slot->first_frame, slot->dropped_frames))
        return -1;
    return send_all(fd, payload, sizeof(payload));
}

static int send_slot(int fd, uint32_t sequence, const struct pcm_slot *slot)
{
    static int16_t output[MAX_BLOCK_FRAMES * CHANNELS];
    uint32_t payload_bytes = slot->frames * CHANNELS * sizeof(int16_t);

    for (uint32_t i = 0; i < slot->frames; i++) {
        float left = slot->samples[i].left * 32768.0f;
        float right = slot->samples[i].right * 32768.0f;
        if (left > 32767.0f)
            left = 32767.0f;
        else if (left < -32768.0f)
            left = -32768.0f;
        if (right > 32767.0f)
            right = 32767.0f;
        else if (right < -32768.0f)
            right = -32768.0f;
        output[i * 2u] = (int16_t)left;
        output[i * 2u + 1u] = (int16_t)right;
    }

    if (send_message_header(fd, 2u, sequence, payload_bytes,
                            slot->first_frame, slot->dropped_frames))
        return -1;
    return send_all(fd, output, payload_bytes);
}

static int open_listener(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int enabled = 1;
    struct sockaddr_in address;

    if (fd < 0)
        return -1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    memset(&address, 0, sizeof(address));
    address.family = AF_INET;
    address.port = big16(PCM_PORT);
    if (bind(fd, &address, sizeof(address)) || listen(fd, 1)) {
        (void)close(fd);
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

static void *stream_worker(void *unused)
{
    int listener;
    int client = -1;
    uint32_t sequence = 0;
    int configured = 0;
    struct timeval timeout = {0, 20000};
    struct timespec idle = {0, 1000000};
    (void)unused;
    (void)nice(10);

    listener = open_listener();
    if (listener < 0) {
        log_line("pcm stream: listen failed\n");
        return 0;
    }

    int ready = open(READY_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (ready >= 0) {
        (void)write(ready, "ready\n", 6);
        (void)close(ready);
    }

    for (;;) {
        if (client < 0) {
            client = accept(listener, 0, 0);
            if (client >= 0) {
                (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                                 &timeout, sizeof(timeout));
                sequence = 0;
                configured = 0;
            }
        }

        uint32_t read = read_index;
        if (read == write_index) {
            (void)nanosleep(&idle, 0);
            continue;
        }

        barrier();
        struct pcm_slot *slot = &pcm_ring[read];
        if (client >= 0) {
            if (!configured) {
                if (send_config(client, sequence++, slot)) {
                    (void)close(client);
                    client = -1;
                } else {
                    configured = 1;
                }
            }
            if (client >= 0 && send_slot(client, sequence++, slot)) {
                (void)close(client);
                client = -1;
            }
        }
        barrier();
        read_index = (read + 1u) & RING_MASK;
    }
}

__attribute__((constructor)) static void initialize(void)
{
    pthread_t worker;

    if (!running_in_rbp())
        return;

    original_copy_buffer = (copy_buffer_fn)install_hook(
        &copy_buffer_hook, WAV_COPY_BUFFER, copy_buffer_guard,
        (void *)hooked_copy_buffer);
    if (!original_copy_buffer) {
        log_line("pcm stream: WavWriter::copyBuffer guard rejected\n");
        return;
    }

    if (pthread_create(&worker, 0, stream_worker, 0)) {
        (void)write_code(copy_buffer_hook.address, copy_buffer_hook.original, 8);
        log_line("pcm stream: worker creation failed\n");
        return;
    }
    (void)pthread_detach(worker);
}
