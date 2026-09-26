// SPDX-License-Identifier: MPL-2.0
/* Firmware 1.19 master-recorder PCM tap and bounded TCP stream.
 *
 * The audio callback is a single producer. It only validates the block, copies
 * it to preallocated rings, publishes one index, and calls the stock method.
 * The worker converts floats to PCM16 and owns every file and socket operation.
 * A full ring drops the incoming block rather than delaying audio playback.
 */

/* The hook is built without a target sysroot, so declare only the ARM libc ABI
 * surface it uses rather than importing host headers. */
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

struct timespec {
    long seconds;
    long nanoseconds;
};

extern int open(const char *, int, ...);
extern ssize_t write(int, const void *, size_t);
extern ssize_t readlink(const char *, char *, size_t);
extern int close(int);
extern int rename(const char *, const char *);
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
extern int poll(void *, unsigned long, int);
extern int clock_gettime(int, struct timespec *);
extern int inet_pton(int, const char *, void *);
extern char *getenv(const char *);
extern int snprintf(char *, size_t, const char *, ...);
extern int *__errno_location(void);
extern int nice(int);
extern int nanosleep(const struct timespec *, struct timespec *);

#define WAV_COPY_BUFFER ((unsigned long)0x0005bb48)
#define READY_PATH "/tmp/rx3-pcm-stream.ready"
#define LOG_PATH "/tmp/rx3-pcm-stream.log"
#define STATUS_PATH "/tmp/rx3-pcm-stream.status"
#define STATUS_TMP_PATH "/tmp/rx3-pcm-stream.status.tmp"

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
#define MSG_NOSIGNAL 0x4000
#define F_GETFL 3
#define F_SETFL 4
#define O_NONBLOCK 04000
#define EINTR 4
#define EAGAIN 11
#define CLOCK_MONOTONIC 1
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020

#define PROTOCOL_VERSION 1u
#define MESSAGE_CONFIG 1u
#define MESSAGE_PCM 2u
#define SAMPLE_FORMAT_S16_LE 1u

#define SEND_ERROR -1
#define SEND_TIMEOUT -2

#define WORKER_WAIT 0u
#define WORKER_RUN 1u
#define WORKER_ABORT 2u

#define ARM_LDR_PC_LITERAL 0xe51ff004u

#define DEFAULT_PCM_PORT 7355u
#define DEFAULT_PCM_BIND "0.0.0.0"
#define SAMPLE_RATE 44100u
#define CHANNELS 2u
#define MAX_BLOCK_FRAMES 4096u
#define SAMPLE_RING_FRAMES 65536u
#define SAMPLE_RING_MASK (SAMPLE_RING_FRAMES - 1u)
#define BLOCK_RING_SLOTS 1024u
#define BLOCK_RING_MASK (BLOCK_RING_SLOTS - 1u)
#define SEND_DEADLINE_MS 20u

typedef struct {
    float left;
    float right;
} Float2;

struct pcm_slot {
    uint64_t first_frame;
    uint32_t frames;
    uint32_t dropped_frames;
    uint32_t sample_index;
};

struct pollfd {
    int fd;
    short events;
    short revents;
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

/* The producer publishes sample data and its descriptor by releasing
 * write_index. The consumer releases both read cursors only after it finishes
 * with those samples. Unsigned cursor subtraction remains valid because each
 * ring capacity is far below half the uint32_t range. */
static Float2 sample_ring[SAMPLE_RING_FRAMES];
static struct pcm_slot pcm_ring[BLOCK_RING_SLOTS];
static uint32_t write_index;
static uint32_t read_index;
static uint32_t sample_write_index;
static uint32_t sample_read_index;
static volatile uint32_t dropped_blocks;
static volatile uint32_t dropped_frames;
static volatile uint32_t captured_blocks;
static volatile uint32_t captured_frames;
static uint64_t producer_frame;
static struct installed_hook copy_buffer_hook;
static copy_buffer_fn original_copy_buffer;
static int listener_fd = -1;
static uint16_t pcm_port = DEFAULT_PCM_PORT;
static char pcm_bind[16] = DEFAULT_PCM_BIND;
static uint32_t worker_command = WORKER_WAIT;

static uint32_t load_acquire(const uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void store_release(uint32_t *target, uint32_t value)
{
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static uint32_t load_relaxed(const volatile uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static void increment_relaxed(volatile uint32_t *value, uint32_t amount)
{
    (void)__atomic_add_fetch(value, amount, __ATOMIC_RELAXED);
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
    trampoline[2] = ARM_LDR_PC_LITERAL;
    trampoline[3] = (uint32_t)(address + 8u);
    clear_instruction_cache((unsigned long)trampoline,
                            (unsigned long)trampoline + 16u);
    if (mprotect(trampoline, 4096, PROT_READ | PROT_EXEC)) {
        (void)munmap(trampoline, 4096);
        return 0;
    }

    patch[0] = ARM_LDR_PC_LITERAL;
    patch[1] = (uint32_t)(unsigned long)replacement;
    original_copy_buffer = (copy_buffer_fn)trampoline;
    if (write_code(address, patch, sizeof(patch))) {
        original_copy_buffer = 0;
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
        uint32_t block_write = load_relaxed(&write_index);
        uint32_t block_read = load_acquire(&read_index);
        uint32_t sample_write = load_relaxed(&sample_write_index);
        uint32_t sample_read = load_acquire(&sample_read_index);
        uint32_t frame_count = (uint32_t)frames;

        if (block_write - block_read >= BLOCK_RING_SLOTS ||
            sample_write - sample_read > SAMPLE_RING_FRAMES - frame_count) {
            increment_relaxed(&dropped_blocks, 1u);
            increment_relaxed(&dropped_frames, frame_count);
        } else {
            uint32_t offset = sample_write & SAMPLE_RING_MASK;
            uint32_t first_copy = SAMPLE_RING_FRAMES - offset;
            struct pcm_slot *slot = &pcm_ring[block_write & BLOCK_RING_MASK];

            if (first_copy > frame_count)
                first_copy = frame_count;
            memcpy(&sample_ring[offset], samples,
                   (size_t)first_copy * sizeof(Float2));
            if (first_copy < frame_count)
                memcpy(sample_ring, samples + first_copy,
                       (size_t)(frame_count - first_copy) * sizeof(Float2));

            slot->first_frame = first_frame;
            slot->frames = frame_count;
            slot->dropped_frames = load_relaxed(&dropped_frames);
            slot->sample_index = sample_write;
            __atomic_store_n(&sample_write_index, sample_write + frame_count,
                             __ATOMIC_RELAXED);
            increment_relaxed(&captured_blocks, 1u);
            increment_relaxed(&captured_frames, frame_count);
            store_release(&write_index, block_write + 1u);
        }
    } else if (frames > 0) {
        increment_relaxed(&dropped_blocks, 1u);
        increment_relaxed(&dropped_frames, (uint32_t)frames);
    }

    return original_copy_buffer(writer, samples, frames);
}

static int timespec_compare(const struct timespec *left,
                            const struct timespec *right)
{
    if (left->seconds != right->seconds)
        return left->seconds < right->seconds ? -1 : 1;
    if (left->nanoseconds != right->nanoseconds)
        return left->nanoseconds < right->nanoseconds ? -1 : 1;
    return 0;
}

static int milliseconds_until(const struct timespec *deadline)
{
    struct timespec now;
    long seconds;
    long nanoseconds;
    uint64_t milliseconds;

    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return -1;
    if (timespec_compare(&now, deadline) >= 0)
        return 0;
    seconds = deadline->seconds - now.seconds;
    nanoseconds = deadline->nanoseconds - now.nanoseconds;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000L;
    }
    milliseconds = (uint64_t)seconds * 1000u;
    milliseconds += ((uint64_t)nanoseconds + 999999u) / 1000000u;
    return milliseconds > 0x7fffffffu ? 0x7fffffff : (int)milliseconds;
}

static int wait_writable(int fd, const struct timespec *deadline)
{
    struct pollfd descriptor;

    descriptor.fd = fd;
    descriptor.events = POLLOUT;
    descriptor.revents = 0;
    for (;;) {
        int timeout = milliseconds_until(deadline);
        int result;

        if (timeout <= 0)
            return SEND_TIMEOUT;
        descriptor.revents = 0;
        result = poll(&descriptor, 1u, timeout);
        if (result > 0) {
            if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
                return SEND_ERROR;
            if (descriptor.revents & POLLOUT)
                return 0;
            continue;
        }
        if (!result)
            return SEND_TIMEOUT;
        if (*__errno_location() != EINTR)
            return SEND_ERROR;
    }
}

static int send_until(int fd, const void *data, size_t length,
                      const struct timespec *deadline)
{
    const uint8_t *cursor = (const uint8_t *)data;

    while (length) {
        if (milliseconds_until(deadline) <= 0)
            return SEND_TIMEOUT;
        ssize_t sent = send(fd, cursor, length, MSG_NOSIGNAL);
        if (sent > 0) {
            cursor += sent;
            length -= (size_t)sent;
            continue;
        }
        if (!sent)
            return SEND_ERROR;
        if (*__errno_location() == EINTR)
            continue;
        if (*__errno_location() != EAGAIN)
            return SEND_ERROR;
        {
            int result = wait_writable(fd, deadline);
            if (result)
                return result;
        }
    }
    return 0;
}

static int send_message(int fd, uint8_t type, uint32_t sequence,
                        uint64_t first_frame, uint32_t dropped,
                        const void *payload, uint32_t payload_bytes)
{
    uint8_t header[28];
    uint32_t value32;
    struct timespec deadline;
    int result;

    if (clock_gettime(CLOCK_MONOTONIC, &deadline))
        return SEND_ERROR;
    deadline.nanoseconds += (long)SEND_DEADLINE_MS * 1000000L;
    if (deadline.nanoseconds >= 1000000000L) {
        deadline.seconds++;
        deadline.nanoseconds -= 1000000000L;
    }

    memset(header, 0, sizeof(header));
    memcpy(header, "RX3A", 4);
    header[4] = PROTOCOL_VERSION;
    header[5] = type;
    value32 = big32(sequence);
    memcpy(header + 8, &value32, 4);
    value32 = big32(payload_bytes);
    memcpy(header + 12, &value32, 4);
    put_big64(header + 16, first_frame);
    value32 = big32(dropped);
    memcpy(header + 24, &value32, 4);

    result = send_until(fd, header, sizeof(header), &deadline);
    if (result || !payload_bytes)
        return result;
    return send_until(fd, payload, payload_bytes, &deadline);
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
    value16 = big16(SAMPLE_FORMAT_S16_LE);
    memcpy(payload + 6, &value16, 2);
    value32 = big32(MAX_BLOCK_FRAMES);
    memcpy(payload + 8, &value32, 4);
    return send_message(fd, MESSAGE_CONFIG, sequence, slot->first_frame,
                        slot->dropped_frames, payload, sizeof(payload));
}

static int16_t pcm16(float sample)
{
    if (!(sample == sample))
        return 0;
    if (sample >= 1.0f)
        return 32767;
    if (sample <= -1.0f)
        return -32768;
    return (int16_t)(sample * 32768.0f);
}

static int send_slot(int fd, uint32_t sequence, const struct pcm_slot *slot)
{
    static int16_t output[MAX_BLOCK_FRAMES * CHANNELS];
    uint32_t payload_bytes = slot->frames * CHANNELS * sizeof(int16_t);
    uint32_t i;

    for (i = 0; i < slot->frames; i++) {
        const Float2 *sample =
            &sample_ring[(slot->sample_index + i) & SAMPLE_RING_MASK];
        output[i * 2u] = pcm16(sample->left);
        output[i * 2u + 1u] = pcm16(sample->right);
    }
    return send_message(fd, MESSAGE_PCM, sequence, slot->first_frame,
                        slot->dropped_frames, output, payload_bytes);
}

static int parse_port(const char *text, uint16_t *port)
{
    uint32_t value = 0;

    if (!text || !*text)
        return -1;
    while (*text) {
        if (*text < '0' || *text > '9')
            return -1;
        value = value * 10u + (uint32_t)(*text - '0');
        if (value > 65535u)
            return -1;
        text++;
    }
    if (value < 1024u)
        return -1;
    *port = (uint16_t)value;
    return 0;
}

static int load_configuration(void)
{
    const char *port = getenv("RX3_PCM_PORT");
    const char *bind_address = getenv("RX3_PCM_BIND");
    uint32_t ignored;
    size_t length = 0;

    if (port && parse_port(port, &pcm_port))
        return -1;
    if (!bind_address || !*bind_address)
        return 0;
    while (bind_address[length]) {
        if (length + 1u >= sizeof(pcm_bind))
            return -1;
        length++;
    }
    if (inet_pton(AF_INET, bind_address, &ignored) != 1)
        return -1;
    memcpy(pcm_bind, bind_address, length + 1u);
    return 0;
}

static int open_listener(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int enabled = 1;
    int flags;
    struct sockaddr_in address;

    if (fd < 0)
        return -1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    memset(&address, 0, sizeof(address));
    address.family = AF_INET;
    address.port = big16(pcm_port);
    if (inet_pton(AF_INET, pcm_bind, &address.address) != 1 ||
        bind(fd, &address, sizeof(address)) || listen(fd, 1)) {
        (void)close(fd);
        return -1;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
        (void)close(fd);
        return -1;
    }
    return fd;
}

static int make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    return flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

struct worker_stats {
    uint64_t streamed_frames;
    uint32_t connections;
    uint32_t disconnects;
    uint32_t send_timeouts;
};

static void publish_status(int client, const struct worker_stats *stats)
{
    char status[512];
    uint32_t queued = load_relaxed(&sample_write_index) -
                      load_acquire(&sample_read_index);
    int length = snprintf(
        status, sizeof(status),
        "state=ready\nbind=%s\nport=%u\nclient=%s\n"
        "captured_blocks=%u\ncaptured_frames=%u\nstreamed_frames=%llu\n"
        "dropped_blocks=%u\ndropped_frames=%u\nqueue_frames=%u\n"
        "queue_capacity_frames=%u\nconnections=%u\ndisconnects=%u\n"
        "send_timeouts=%u\n",
        pcm_bind, (unsigned int)pcm_port,
        client >= 0 ? "connected" : "disconnected",
        load_relaxed(&captured_blocks), load_relaxed(&captured_frames),
        (unsigned long long)stats->streamed_frames,
        load_relaxed(&dropped_blocks), load_relaxed(&dropped_frames), queued,
        SAMPLE_RING_FRAMES, stats->connections, stats->disconnects,
        stats->send_timeouts);
    int fd;
    size_t offset = 0;

    if (length <= 0 || (size_t)length >= sizeof(status))
        return;
    fd = open(STATUS_TMP_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return;
    while (offset < (size_t)length) {
        ssize_t written = write(fd, status + offset, (size_t)length - offset);
        if (written > 0) {
            offset += (size_t)written;
            continue;
        }
        if (written < 0 && *__errno_location() == EINTR)
            continue;
        (void)close(fd);
        return;
    }
    (void)close(fd);
    (void)rename(STATUS_TMP_PATH, STATUS_PATH);
}

static void disconnect_client(int *client, int result,
                              struct worker_stats *stats)
{
    if (*client < 0)
        return;
    (void)close(*client);
    *client = -1;
    stats->disconnects++;
    if (result == SEND_TIMEOUT)
        stats->send_timeouts++;
}

static void *stream_worker(void *unused)
{
    int client = -1;
    uint32_t sequence = 0;
    int configured = 0;
    struct timespec idle = {0, 1000000};
    struct timespec next_status;
    struct worker_stats stats;
    uint32_t command;
    (void)unused;
    (void)nice(10);
    memset(&stats, 0, sizeof(stats));

    do {
        command = load_acquire(&worker_command);
        if (!command)
            (void)nanosleep(&idle, 0);
    } while (!command);
    if (command != WORKER_RUN)
        return 0;

    (void)clock_gettime(CLOCK_MONOTONIC, &next_status);

    {
        int ready = open(READY_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (ready >= 0) {
            (void)write(ready, "ready\n", 6);
            (void)close(ready);
        }
    }
    publish_status(client, &stats);

    for (;;) {
        struct timespec now;
        uint32_t read;

        if (client < 0) {
            int accepted = accept(listener_fd, 0, 0);
            if (accepted >= 0) {
                if (make_nonblocking(accepted)) {
                    (void)close(accepted);
                } else {
                    client = accepted;
                    sequence = 0;
                    configured = 0;
                    stats.connections++;
                }
            }
        }

        read = load_relaxed(&read_index);
        if (read != load_acquire(&write_index)) {
            struct pcm_slot *slot = &pcm_ring[read & BLOCK_RING_MASK];

            if (client >= 0 && !configured) {
                int result = send_config(client, sequence++, slot);
                if (result)
                    disconnect_client(&client, result, &stats);
                else
                    configured = 1;
            }
            if (client >= 0) {
                int result = send_slot(client, sequence++, slot);
                if (result)
                    disconnect_client(&client, result, &stats);
                else
                    stats.streamed_frames += slot->frames;
            }
            store_release(&sample_read_index,
                          slot->sample_index + slot->frames);
            store_release(&read_index, read + 1u);
        } else {
            (void)nanosleep(&idle, 0);
        }

        if (!clock_gettime(CLOCK_MONOTONIC, &now) &&
            timespec_compare(&now, &next_status) >= 0) {
            publish_status(client, &stats);
            next_status = now;
            next_status.seconds++;
        }
    }
}

__attribute__((constructor)) static void initialize(void)
{
    pthread_t worker;

    if (!running_in_rbp())
        return;
    if (load_configuration()) {
        log_line("pcm stream: invalid RX3_PCM_PORT or RX3_PCM_BIND\n");
        return;
    }
    listener_fd = open_listener();
    if (listener_fd < 0) {
        log_line("pcm stream: listen failed\n");
        return;
    }

    if (pthread_create(&worker, 0, stream_worker, 0)) {
        (void)close(listener_fd);
        listener_fd = -1;
        log_line("pcm stream: worker creation failed\n");
        return;
    }
    (void)pthread_detach(worker);

    original_copy_buffer = (copy_buffer_fn)install_hook(
        &copy_buffer_hook, WAV_COPY_BUFFER, copy_buffer_guard,
        (void *)hooked_copy_buffer);
    if (!original_copy_buffer) {
        store_release(&worker_command, WORKER_ABORT);
        (void)close(listener_fd);
        listener_fd = -1;
        log_line("pcm stream: WavWriter::copyBuffer guard rejected\n");
        return;
    }
    store_release(&worker_command, WORKER_RUN);
}
