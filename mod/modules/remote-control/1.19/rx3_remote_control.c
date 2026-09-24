// SPDX-License-Identifier: MPL-2.0
/* Guarded bidirectional KeyManager bridge for verified RX3 firmware 1.19. */

typedef unsigned int size_t;
typedef int ssize_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long pthread_t;

#include "../../../../tools/rx3_remote/rx3_remote_protocol.h"

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

struct pollfd {
    int fd;
    short events;
    short revents;
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
extern void *memmove(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern int memcmp(const void *, const void *, size_t);
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_detach(pthread_t);
extern int socket(int, int, int);
extern int socketpair(int, int, int, int[2]);
extern int bind(int, const void *, unsigned int);
extern int listen(int, int);
extern int accept(int, void *, void *);
extern int setsockopt(int, int, int, const void *, unsigned int);
extern ssize_t send(int, const void *, size_t, int);
extern ssize_t recv(int, void *, size_t, int);
extern int poll(struct pollfd *, unsigned long, int);
extern int fcntl(int, int, ...);
extern int clock_gettime(int, struct timespec *);

#define KEY_MANAGER_SEND_KEY ((unsigned long)0x0037ad64)
#define READY_PATH "/tmp/rx3-remote-control.ready"
#define LOG_PATH "/tmp/rx3-remote-control.log"

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
#define AF_UNIX 1
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_NONBLOCK 04000
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_SNDBUF 7
#define SO_SNDTIMEO 21
#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000
#define F_GETFL 3
#define F_SETFL 4
#define O_NONBLOCK 04000
#define POLLIN 0x0001
#define POLLERR 0x0008
#define POLLHUP 0x0010
#define POLLNVAL 0x0020
#define CLOCK_MONOTONIC 1

#define EVENT_FLAG_DROPPED_BEFORE 1u
#define EVENT_QUEUE_BYTES 2048u
#define RECEIVE_BYTES (RX3R_MAX_PAYLOAD + 512u)
#define PENDING_SLOTS 64u
#define PENDING_MASK (PENDING_SLOTS - 1u)

typedef void (*send_key_fn)(void *, int, int, int, long, float, long);

struct installed_hook {
    unsigned long address;
    uint8_t original[8];
    void *trampoline;
};

struct native_control {
    uint16_t key_code;
    uint8_t operation;
    uint8_t channel;
    int value;
    uint32_t float_bits;
    int auxiliary;
};

struct queued_event {
    struct native_control control;
    uint8_t source;
    uint8_t flags;
    uint16_t reserved;
    uint64_t timestamp_us;
};

struct pending_remote {
    struct native_control control;
    uint64_t queued_us;
    uint8_t emitted;
};

static const uint8_t send_key_guard[8] = {
    0xf0, 0x4f, 0x2d, 0xe9, 0x0c, 0xd0, 0x4d, 0xe2
};

static struct installed_hook send_key_hook;
static send_key_fn original_send_key;
static int event_socket[2] = {-1, -1};
static void *volatile key_manager;
static volatile uint32_t dropped_events;
static struct pending_remote pending_remote[PENDING_SLOTS];
static volatile uint32_t pending_write;
static volatile uint32_t pending_read;

static void barrier(void)
{
    __sync_synchronize();
}

static uint16_t get_big16(const uint8_t *input)
{
    return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static uint32_t get_big32(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) | input[3];
}

static void put_big16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8);
    output[1] = (uint8_t)value;
}

static void put_big32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static void put_big64(uint8_t *output, uint64_t value)
{
    put_big32(output, (uint32_t)(value >> 32));
    put_big32(output + 4, (uint32_t)value);
}

static uint64_t monotonic_us(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    return (uint64_t)(uint32_t)now.seconds * 1000000u +
           (uint32_t)now.nanoseconds / 1000u;
}

static void log_line(const char *message)
{
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
    size_t length = 0;
    if (fd < 0)
        return;
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
    unsigned long mask;
    unsigned long first;
    unsigned long last;
    size_t span;

    if (page_size <= 0)
        page_size = 4096;
    mask = (unsigned long)page_size - 1u;
    first = address & ~mask;
    last = (address + length - 1u) & ~mask;
    span = (size_t)(last - first) + (size_t)page_size;
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

static int is_message_thread(void *manager)
{
    void *message_manager = *(void **)((uint8_t *)manager + 4u);
    void **vtable;
    typedef int (*is_message_thread_fn)(void *);

    if (!message_manager)
        return 0;
    vtable = *(void ***)message_manager;
    return ((is_message_thread_fn)vtable[3])(message_manager) != 0;
}

static int same_control(const struct native_control *left,
                        const struct native_control *right)
{
    return left->key_code == right->key_code &&
           left->operation == right->operation &&
           left->channel == right->channel && left->value == right->value &&
           left->float_bits == right->float_bits &&
           left->auxiliary == right->auxiliary;
}

static uint8_t event_source(const struct native_control *control,
                            uint64_t timestamp_us, int on_message_thread)
{
    uint32_t read = pending_read;
    uint32_t write = pending_write;

    while (read != write) {
        struct pending_remote *pending = &pending_remote[read];
        barrier();
        if (timestamp_us - pending->queued_us > 2000000u) {
            read = (read + 1u) & PENDING_MASK;
            pending_read = read;
            continue;
        }
        if (!same_control(control, &pending->control))
            break;
        if (pending->emitted) {
            pending_read = (read + 1u) & PENDING_MASK;
            return 0;
        }
        pending->emitted = 1u;
        barrier();
        if (on_message_thread)
            pending_read = (read + 1u) & PENDING_MASK;
        return RX3R_SOURCE_REMOTE;
    }
    return on_message_thread ? RX3R_SOURCE_PHYSICAL : 0;
}

static void queue_event(const struct native_control *control,
                        int on_message_thread)
{
    struct queued_event event;
    ssize_t sent;

    memset(&event, 0, sizeof(event));
    event.control = *control;
    event.timestamp_us = monotonic_us();
    event.source = event_source(control, event.timestamp_us,
                                on_message_thread);
    if (!event.source)
        return;
    if (__sync_lock_test_and_set(&dropped_events, 0u))
        event.flags |= EVENT_FLAG_DROPPED_BEFORE;
    sent = send(event_socket[0], &event, sizeof(event), MSG_DONTWAIT);
    if (sent != (ssize_t)sizeof(event))
        __sync_fetch_and_add(&dropped_events, 1u);
}

static void hooked_send_key(void *manager, int key_code, int operation,
                            int channel, long value, float float_value,
                            long auxiliary)
{
    struct native_control control;
    int on_message_thread;

    if (*(void **)((uint8_t *)manager + 0x84u)) {
        key_manager = manager;
        barrier();
    }
    control.key_code = (uint16_t)key_code;
    control.operation = (uint8_t)operation;
    control.channel = (uint8_t)channel;
    control.value = (int)value;
    memcpy(&control.float_bits, &float_value, sizeof(control.float_bits));
    control.auxiliary = (int)auxiliary;
    on_message_thread = is_message_thread(manager);
    queue_event(&control, on_message_thread);
    original_send_key(manager, key_code, operation, channel,
                      value, float_value, auxiliary);
}

static int key_is_known(uint16_t key)
{
    switch (key) {
    case RX3R_CONTROL_BROWSER_SOURCE:
    case RX3R_CONTROL_BROWSER_BROWSE:
    case RX3R_CONTROL_RECORDER_USB_RECORD:
    case RX3R_CONTROL_RECORDER_TRACK_MARK:
    case RX3R_CONTROL_BEAT_FX_QUANTIZE:
    case RX3R_CONTROL_MIC_SWITCH:
    case RX3R_CONTROL_MIC_EQ_HIGH:
    case RX3R_CONTROL_MIC_EQ_LOW:
    case RX3R_CONTROL_DECK_PLAY_PAUSE:
    case RX3R_CONTROL_DECK_CUE:
    case RX3R_CONTROL_DECK_SHIFT:
    case RX3R_CONTROL_DECK_VINYL_MODE:
    case RX3R_CONTROL_DECK_TEMPO_RANGE:
    case RX3R_CONTROL_DECK_MASTER_TEMPO:
    case RX3R_CONTROL_DECK_TEMPO_SLIDER:
    case RX3R_CONTROL_DECK_TIME_MODE_AUTO_CUE:
    case RX3R_CONTROL_DECK_QUANTIZE:
    case RX3R_CONTROL_DECK_LOOP_IN:
    case RX3R_CONTROL_DECK_LOOP_OUT:
    case RX3R_CONTROL_DECK_RELOOP_EXIT:
    case RX3R_CONTROL_DECK_REVERSE:
    case RX3R_CONTROL_DECK_SLIP:
    case RX3R_CONTROL_DECK_MASTER:
    case RX3R_CONTROL_DECK_SYNC:
    case RX3R_CONTROL_DECK_HOT_CUE_MODE:
    case RX3R_CONTROL_DECK_AUTO_BEAT_LOOP:
    case RX3R_CONTROL_DECK_SLIP_LOOP_MODE:
    case RX3R_CONTROL_DECK_BEAT_JUMP:
    case RX3R_CONTROL_DECK_PAD_1:
    case RX3R_CONTROL_DECK_PAD_2:
    case RX3R_CONTROL_DECK_PAD_3:
    case RX3R_CONTROL_DECK_PAD_4:
    case RX3R_CONTROL_DECK_PAD_5:
    case RX3R_CONTROL_DECK_PAD_6:
    case RX3R_CONTROL_DECK_PAD_7:
    case RX3R_CONTROL_DECK_PAD_8:
    case RX3R_CONTROL_DECK_SEARCH_FORWARD:
    case RX3R_CONTROL_DECK_SEARCH_REVERSE:
    case RX3R_CONTROL_DECK_VINYL_SPEED_ADJUST:
    case RX3R_CONTROL_DECK_CUE_DELETE:
    case RX3R_CONTROL_DECK_CUE_MEMORY:
    case RX3R_CONTROL_DECK_NEEDLE_SEARCH:
    case RX3R_CONTROL_BROWSER_ROTARY_SELECTOR:
    case RX3R_CONTROL_BROWSER_BACK:
    case RX3R_CONTROL_BROWSER_TAG_TRACK:
    case RX3R_CONTROL_BROWSER_TRACK_FILTER:
    case RX3R_CONTROL_BROWSER_DECK_SELECT:
    case RX3R_CONTROL_BROWSER_TRACK_FORWARD:
    case RX3R_CONTROL_BROWSER_TRACK_REVERSE:
    case RX3R_CONTROL_DECK_JOG:
    case RX3R_CONTROL_DECK_JOG_TOUCH:
    case RX3R_CONTROL_BROWSER_LOAD:
    case RX3R_CONTROL_DECK_CALL_NEXT:
    case RX3R_CONTROL_DECK_CALL_PREVIOUS:
    case RX3R_CONTROL_MASTER_LEVEL:
    case RX3R_CONTROL_BOOTH_LEVEL:
    case RX3R_CONTROL_HEADPHONES_MIX:
    case RX3R_CONTROL_HEADPHONES_LEVEL:
    case RX3R_CONTROL_MASTER_CUE:
    case RX3R_CONTROL_LINK_CUE:
    case RX3R_CONTROL_AUX_GAIN:
    case RX3R_CONTROL_AUX_LEVEL:
    case RX3R_CONTROL_BEAT_FX_SWITCH:
    case RX3R_CONTROL_BEAT_FX_CHANNEL:
    case RX3R_CONTROL_BEAT_FX_ON_OFF:
    case RX3R_CONTROL_BEAT_FX_TIME:
    case RX3R_CONTROL_BEAT_FX_DEPTH:
    case RX3R_CONTROL_BEAT_FX_PREVIOUS:
    case RX3R_CONTROL_BEAT_FX_NEXT:
    case RX3R_CONTROL_BEAT_FX_TAP:
    case RX3R_CONTROL_MIXER_TRIM:
    case RX3R_CONTROL_MIXER_EQ_HIGH:
    case RX3R_CONTROL_MIXER_EQ_MID:
    case RX3R_CONTROL_MIXER_EQ_LOW:
    case RX3R_CONTROL_MIXER_CHANNEL_FADER:
    case RX3R_CONTROL_MIXER_INPUT_SOURCE:
    case RX3R_CONTROL_MIXER_CUE:
    case RX3R_CONTROL_SOUND_COLOR_FX_KNOB:
    case RX3R_CONTROL_SOUND_COLOR_FX_SPACE:
    case RX3R_CONTROL_SOUND_COLOR_FX_DUB_ECHO:
    case RX3R_CONTROL_SOUND_COLOR_FX_SWEEP:
    case RX3R_CONTROL_SOUND_COLOR_FX_NOISE:
    case RX3R_CONTROL_SOUND_COLOR_FX_CRUSH:
    case RX3R_CONTROL_SOUND_COLOR_FX_FILTER:
    case RX3R_CONTROL_SOUND_COLOR_FX_PARAMETER:
    case RX3R_CONTROL_MIXER_CROSSFADER:
    case RX3R_CONTROL_MIXER_CROSSFADER_CURVE:
        return 1;
    default:
        return 0;
    }
}

static int key_uses_channel(uint16_t key)
{
    if ((key >= RX3R_CONTROL_DECK_PLAY_PAUSE &&
         key <= RX3R_CONTROL_DECK_NEEDLE_SEARCH) ||
        key == RX3R_CONTROL_DECK_JOG ||
        key == RX3R_CONTROL_DECK_JOG_TOUCH ||
        key == RX3R_CONTROL_BROWSER_LOAD ||
        key == RX3R_CONTROL_DECK_CALL_NEXT ||
        key == RX3R_CONTROL_DECK_CALL_PREVIOUS ||
        (key >= RX3R_CONTROL_MIXER_TRIM &&
         key <= RX3R_CONTROL_MIXER_CUE) ||
        key == RX3R_CONTROL_SOUND_COLOR_FX_KNOB)
        return 1;
    return 0;
}

static int valid_command(const struct native_control *control)
{
    uint32_t exponent = control->float_bits & 0x7f800000u;
    if (!key_is_known(control->key_code) ||
        control->operation > RX3R_OP_VALUE_CHANGED)
        return 0;
    if (key_uses_channel(control->key_code)) {
        if (control->channel < 1u || control->channel > 2u)
            return 0;
    } else if (control->channel != 0u) {
        return 0;
    }
    if (control->operation == RX3R_OP_ABSOLUTE_MOVED &&
        control->key_code != RX3R_CONTROL_DECK_TEMPO_SLIDER &&
        (control->value < 0 || control->value > 1023))
        return 0;
    return exponent != 0x7f800000u;
}

static int reserve_remote(const struct native_control *control)
{
    uint32_t write_index = pending_write;
    uint32_t next = (write_index + 1u) & PENDING_MASK;
    if (next == pending_read)
        return -1;
    pending_remote[write_index].control = *control;
    pending_remote[write_index].queued_us = monotonic_us();
    pending_remote[write_index].emitted = 0u;
    barrier();
    pending_write = next;
    return 0;
}

static int send_all(int fd, const void *data, size_t length)
{
    const uint8_t *cursor = data;
    while (length) {
        ssize_t sent = send(fd, cursor, length, MSG_NOSIGNAL);
        if (sent <= 0)
            return -1;
        cursor += sent;
        length -= (size_t)sent;
    }
    return 0;
}

static int send_frame(int fd, uint8_t type, uint32_t request_id,
                      const void *payload, uint32_t length)
{
    struct rx3r_header header;
    memcpy(header.magic, "RX3R", 4);
    header.version = RX3R_VERSION;
    header.type = type;
    put_big16(header.flags_be, 0);
    put_big32(header.request_id_be, request_id);
    put_big32(header.payload_length_be, length);
    if (send_all(fd, &header, sizeof(header)))
        return -1;
    return length ? send_all(fd, payload, length) : 0;
}

static int send_error(int fd, uint32_t request_id, uint32_t code,
                      uint32_t detail)
{
    struct rx3r_error payload;
    put_big32(payload.code_be, code);
    put_big32(payload.detail_be, detail);
    return send_frame(fd, RX3R_ERROR, request_id, &payload, sizeof(payload));
}

static int send_ack(int fd, uint32_t request_id, uint32_t result)
{
    struct rx3r_result payload;
    put_big32(payload.result_be, result);
    return send_frame(fd, RX3R_ACK, request_id, &payload, sizeof(payload));
}

static int send_handshake(int fd)
{
    struct rx3r_hello hello;
    struct rx3r_schema schema;
    static const uint8_t prefix[4] = {0xcf, 0x30, 0x92, 0x38};

    memcpy(hello.rbp_build_prefix, prefix, sizeof(prefix));
    put_big16(hello.firmware_major_be, 1u);
    put_big16(hello.firmware_minor_be, 19u);
    put_big32(hello.schema_revision_be, RX3R_SCHEMA_REVISION);
    put_big32(hello.capabilities_be,
              RX3R_CAP_EVENTS | RX3R_CAP_COMMANDS | RX3R_CAP_SCHEMA);
    if (send_frame(fd, RX3R_HELLO, 0, &hello, sizeof(hello)))
        return -1;
    put_big32(schema.crc32_be, RX3R_SCHEMA_CRC32);
    put_big32(schema.control_count_be, RX3R_CONTROL_COUNT);
    return send_frame(fd, RX3R_SCHEMA, 0, &schema, sizeof(schema));
}

static int send_control_event(int fd, const struct queued_event *event)
{
    struct rx3r_control_event payload;
    put_big16(payload.control.key_code_be, event->control.key_code);
    payload.control.operation = event->control.operation;
    payload.control.channel = event->control.channel;
    put_big32(payload.control.value_be, (uint32_t)event->control.value);
    put_big32(payload.control.float_bits_be, event->control.float_bits);
    put_big32(payload.control.auxiliary_be,
              (uint32_t)event->control.auxiliary);
    payload.source = event->source;
    payload.flags = event->flags;
    payload.reserved[0] = 0;
    payload.reserved[1] = 0;
    put_big64(payload.timestamp_us_be, event->timestamp_us);
    return send_frame(fd, RX3R_EVENT, 0, &payload, sizeof(payload));
}

static void decode_control(struct native_control *output, const uint8_t *input)
{
    output->key_code = get_big16(input);
    output->operation = input[2];
    output->channel = input[3];
    output->value = (int)get_big32(input + 4);
    output->float_bits = get_big32(input + 8);
    output->auxiliary = (int)get_big32(input + 12);
}

static int handle_frame(int fd, uint8_t type, uint32_t request_id,
                        const uint8_t *payload, uint32_t length,
                        uint32_t *subscription)
{
    struct native_control control;
    float float_value;
    void *manager;

    if (type == RX3R_SUBSCRIBE) {
        if (length != sizeof(struct rx3r_subscription))
            return send_error(fd, request_id, RX3R_ERROR_INVALID_MESSAGE, length);
        *subscription = get_big32(payload) & RX3R_EVENT_CONTROLS;
        return 0;
    }
    if (type == RX3R_PING)
        return send_frame(fd, RX3R_PONG, request_id, payload, length);
    if (type != RX3R_COMMAND || length != sizeof(struct rx3r_control) ||
        request_id == 0)
        return send_error(fd, request_id, RX3R_ERROR_INVALID_MESSAGE, type);

    decode_control(&control, payload);
    if (!key_is_known(control.key_code))
        return send_error(fd, request_id, RX3R_ERROR_UNSUPPORTED_CONTROL,
                          control.key_code);
    if (!valid_command(&control))
        return send_error(fd, request_id, RX3R_ERROR_INVALID_COMMAND,
                          control.key_code);
    manager = key_manager;
    if (!manager)
        return send_error(fd, request_id, RX3R_ERROR_BUSY, 0);
    if (reserve_remote(&control))
        return send_error(fd, request_id, RX3R_ERROR_BUSY, control.key_code);

    memcpy(&float_value, &control.float_bits, sizeof(float_value));
    hooked_send_key(manager, control.key_code, control.operation,
                    control.channel, control.value, float_value,
                    control.auxiliary);
    return send_ack(fd, request_id, 0);
}

static int consume_input(int fd, uint8_t *buffer, uint32_t *used,
                         uint32_t *subscription)
{
    ssize_t received = recv(fd, buffer + *used, RECEIVE_BYTES - *used,
                            MSG_DONTWAIT);
    uint32_t offset = 0;
    if (received <= 0)
        return -1;
    *used += (uint32_t)received;

    while (*used - offset >= sizeof(struct rx3r_header)) {
        struct rx3r_header *header = (struct rx3r_header *)(buffer + offset);
        uint32_t length;
        uint32_t frame_length;
        uint32_t request_id;

        if (memcmp(header->magic, "RX3R", 4) ||
            header->version != RX3R_VERSION)
            return -1;
        length = get_big32(header->payload_length_be);
        if (length > RX3R_MAX_PAYLOAD)
            return -1;
        frame_length = (uint32_t)sizeof(*header) + length;
        if (*used - offset < frame_length)
            break;
        request_id = get_big32(header->request_id_be);
        if (handle_frame(fd, header->type, request_id,
                         buffer + offset + sizeof(*header), length,
                         subscription))
            return -1;
        offset += frame_length;
    }
    if (offset) {
        memmove(buffer, buffer + offset, *used - offset);
        *used -= offset;
    }
    if (*used == RECEIVE_BYTES)
        return -1;
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
    address.port = (uint16_t)((RX3R_PORT >> 8) | (RX3R_PORT << 8));
    if (bind(fd, &address, sizeof(address)) || listen(fd, 1)) {
        (void)close(fd);
        return -1;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

static void close_client(int *client, uint32_t *used, uint32_t *subscription)
{
    if (*client >= 0)
        (void)close(*client);
    *client = -1;
    *used = 0;
    *subscription = 0;
}

static void *remote_worker(void *unused)
{
    int listener;
    int client = -1;
    uint8_t receive_buffer[RECEIVE_BYTES];
    uint32_t receive_used = 0;
    uint32_t subscription = 0;
    struct timeval timeout = {0, 20000};
    (void)unused;

    listener = open_listener();
    if (listener < 0) {
        log_line("remote control: listen failed\n");
        return 0;
    }
    {
        int ready = open(READY_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (ready >= 0) {
            (void)write(ready, "ready\n", 6);
            (void)close(ready);
        }
    }

    for (;;) {
        struct pollfd descriptors[3];
        int count = 2;
        descriptors[0].fd = listener;
        descriptors[0].events = POLLIN;
        descriptors[0].revents = 0;
        descriptors[1].fd = event_socket[1];
        descriptors[1].events = POLLIN;
        descriptors[1].revents = 0;
        if (client >= 0) {
            descriptors[2].fd = client;
            descriptors[2].events = POLLIN;
            descriptors[2].revents = 0;
            count = 3;
        }
        if (poll(descriptors, (unsigned long)count, 250) < 0)
            continue;

        if (descriptors[0].revents & POLLIN) {
            int accepted = accept(listener, 0, 0);
            if (accepted >= 0) {
                if (client >= 0) {
                    (void)send_error(accepted, 0, RX3R_ERROR_BUSY, 0);
                    (void)close(accepted);
                } else {
                    client = accepted;
                    (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                                     &timeout, sizeof(timeout));
                    if (send_handshake(client))
                        close_client(&client, &receive_used, &subscription);
                }
            }
        }
        if ((descriptors[1].revents & POLLIN) != 0) {
            struct queued_event event;
            ssize_t length;
            while ((length = recv(event_socket[1], &event, sizeof(event),
                                  MSG_DONTWAIT)) > 0) {
                if (length != (ssize_t)sizeof(event) || client < 0 ||
                    !(subscription & RX3R_EVENT_CONTROLS))
                    continue;
                if (send_control_event(client, &event))
                    close_client(&client, &receive_used, &subscription);
            }
        }
        if (client >= 0 && count == 3) {
            short client_events = descriptors[2].revents;
            if (client_events & (POLLERR | POLLHUP | POLLNVAL)) {
                close_client(&client, &receive_used, &subscription);
            } else if ((client_events & POLLIN) &&
                       consume_input(client, receive_buffer, &receive_used,
                                     &subscription)) {
                close_client(&client, &receive_used, &subscription);
            }
        }
    }
}

__attribute__((constructor)) static void initialize(void)
{
    pthread_t worker;
    int buffer_bytes = EVENT_QUEUE_BYTES;

    if (!running_in_rbp())
        return;
    if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, event_socket)) {
        log_line("remote control: event queue creation failed\n");
        return;
    }
    (void)setsockopt(event_socket[0], SOL_SOCKET, SO_SNDBUF, &buffer_bytes,
                     sizeof(buffer_bytes));
    original_send_key = (send_key_fn)install_hook(
        &send_key_hook, KEY_MANAGER_SEND_KEY, send_key_guard,
        (void *)hooked_send_key);
    if (!original_send_key) {
        log_line("remote control: sendKey guard rejected\n");
        (void)close(event_socket[0]);
        (void)close(event_socket[1]);
        return;
    }
    if (pthread_create(&worker, 0, remote_worker, 0)) {
        (void)write_code(send_key_hook.address, send_key_hook.original, 8);
        log_line("remote control: worker creation failed\n");
        (void)close(event_socket[0]);
        (void)close(event_socket[1]);
        return;
    }
    (void)pthread_detach(worker);
}
