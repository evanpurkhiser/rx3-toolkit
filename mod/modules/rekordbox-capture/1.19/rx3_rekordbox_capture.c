// SPDX-License-Identifier: MPL-2.0
/* Passive rekordbox protocol capture for one verified XDJ-RX3 1.19 rbp.
 *
 * Hook callbacks copy bounded records to a nonblocking local datagram.  The
 * worker owns timestamps, JSON encoding, and all media I/O.  A full queue
 * drops records instead of delaying rbp's HID or MIDI threads.
 */

typedef unsigned int size_t;
typedef int ssize_t;
typedef long off_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long pthread_t;

struct timespec {
    long tv_sec;
    long tv_nsec;
};

extern int open(const char *, int, ...);
extern ssize_t write(int, const void *, size_t);
extern ssize_t readlink(const char *, char *, size_t);
extern int close(int);
extern off_t lseek(int, off_t, int);
extern void *mmap(void *, size_t, int, int, int, off_t);
extern int munmap(void *, size_t);
extern int mprotect(void *, size_t, int);
extern long sysconf(int);
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern int memcmp(const void *, const void *, size_t);
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_detach(pthread_t);
extern int socketpair(int, int, int, int[2]);
extern ssize_t send(int, const void *, size_t, int);
extern ssize_t recv(int, void *, size_t, int);
extern int clock_gettime(int, struct timespec *);
extern char *getenv(const char *);

#define O_WRONLY 1
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_APPEND 02000
#define SEEK_END 2
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void *)-1)
#define _SC_PAGESIZE 30
#define AF_UNIX 1
#define SOCK_DGRAM 2
#define MSG_DONTWAIT 0x40
#define CLOCK_MONOTONIC 1

#define CAPTURE_PATH_ENV "RX3_REKORDBOX_CAPTURE_PATH"
#define READY_PATH "/tmp/rx3-rekordbox-capture.ready"
#define LOG_PATH "/tmp/rx3-rekordbox-capture.log"
#define CAPTURE_LIMIT (64u * 1024u * 1024u)
#define PAYLOAD_LIMIT 256u

#define HID_INCOMING ((unsigned long)0x002eb864)
#define HID_OUTGOING ((unsigned long)0x00029568)
#define MIDI_INCOMING ((unsigned long)0x002f0238)

#define RECORD_HID_HOST_TO_RX3 1u
#define RECORD_HID_RX3_TO_HOST 2u
#define RECORD_MIDI_HOST_TO_RX3 3u

typedef void (*hid_message_fn)(void *, uint8_t *, int);
typedef void (*midi_message_fn)(void *, void *, const void *);

struct installed_hook {
    unsigned long address;
    uint8_t original[8];
    void *trampoline;
};

struct capture_record {
    uint32_t sequence;
    uint32_t original_length;
    uint16_t captured_length;
    uint8_t kind;
    uint8_t truncated;
    uint8_t payload[PAYLOAD_LIMIT];
};

struct line_buffer {
    char data[1024];
    unsigned int length;
    uint8_t failed;
};

static const uint8_t hid_incoming_guard[8] = {
    0x40, 0x00, 0x52, 0xe3, 0xf0, 0x41, 0x2d, 0xe9
};
static const uint8_t hid_outgoing_guard[8] = {
    0xf0, 0x41, 0x2d, 0xe9, 0x00, 0x50, 0xa0, 0xe1
};
static const uint8_t midi_incoming_guard[8] = {
    0x22, 0x30, 0xd0, 0xe5, 0xf0, 0x45, 0x2d, 0xe9
};

static struct installed_hook hid_incoming_hook;
static struct installed_hook hid_outgoing_hook;
static struct installed_hook midi_incoming_hook;
static hid_message_fn original_hid_incoming;
static hid_message_fn original_hid_outgoing;
static midi_message_fn original_midi_incoming;
static int capture_socket[2] = {-1, -1};
static volatile unsigned int next_sequence;
static volatile unsigned int dropped_records;

static unsigned int string_length(const char *text)
{
    unsigned int length = 0;
    while (text[length])
        length++;
    return length;
}

static int write_all(int fd, const char *data, unsigned int length)
{
    unsigned int written = 0;
    while (written < length) {
        ssize_t count = write(fd, data + written, length - written);
        if (count <= 0)
            return 0;
        written += (unsigned int)count;
    }
    return 1;
}

static void log_line(const char *text)
{
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return;
    (void)write_all(fd, text, string_length(text));
    (void)write_all(fd, "\n", 1u);
    close(fd);
}

static void publish_ready(const char *capture_path)
{
    int fd = open(READY_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return;
    (void)write_all(fd, capture_path, string_length(capture_path));
    (void)write_all(fd, "\n", 1u);
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
    if (mprotect((void *)first, span, PROT_READ | PROT_EXEC))
        return -1;
    return 0;
}

static void *install_hook(struct installed_hook *hook, unsigned long address,
                          const uint8_t guard[8], void *replacement)
{
    if (memcmp((const void *)address, guard, 8u))
        return 0;

    uint32_t *trampoline = mmap(0, 4096u, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED)
        return 0;
    memcpy(trampoline, (const void *)address, 8u);
    trampoline[2] = 0xe51ff004u;
    trampoline[3] = (uint32_t)(address + 8u);
    clear_instruction_cache((unsigned long)trampoline,
                            (unsigned long)trampoline + 16u);
    if (mprotect(trampoline, 4096u, PROT_READ | PROT_EXEC)) {
        munmap(trampoline, 4096u);
        return 0;
    }

    uint32_t patch[2] = {0xe51ff004u, (uint32_t)(unsigned long)replacement};
    if (write_code(address, patch, sizeof(patch))) {
        munmap(trampoline, 4096u);
        return 0;
    }
    hook->address = address;
    memcpy(hook->original, guard, sizeof(hook->original));
    hook->trampoline = trampoline;
    return trampoline;
}

static void uninstall_hook(struct installed_hook *hook)
{
    if (!hook->address)
        return;
    (void)write_code(hook->address, hook->original, sizeof(hook->original));
    if (hook->trampoline)
        munmap(hook->trampoline, 4096u);
    memset(hook, 0, sizeof(*hook));
}

static void queue_record(uint8_t kind, const uint8_t *payload, int length)
{
    struct capture_record record;
    unsigned int safe_length = length > 0 ? (unsigned int)length : 0u;
    unsigned int captured = safe_length;
    if (captured > PAYLOAD_LIMIT)
        captured = PAYLOAD_LIMIT;

    memset(&record, 0, sizeof(record));
    record.sequence = __sync_fetch_and_add(&next_sequence, 1u);
    record.original_length = safe_length;
    record.captured_length = (uint16_t)captured;
    record.kind = kind;
    record.truncated = captured != safe_length;
    if (payload && captured)
        memcpy(record.payload, payload, captured);

    if (capture_socket[0] < 0 ||
        send(capture_socket[0], &record, sizeof(record), MSG_DONTWAIT) !=
            (ssize_t)sizeof(record))
        (void)__sync_add_and_fetch(&dropped_records, 1u);
}

static void hooked_hid_incoming(void *self, uint8_t *payload, int length)
{
    queue_record(RECORD_HID_HOST_TO_RX3, payload, length);
    original_hid_incoming(self, payload, length);
}

static void hooked_hid_outgoing(void *self, uint8_t *payload, int length)
{
    queue_record(RECORD_HID_RX3_TO_HOST, payload, length);
    original_hid_outgoing(self, payload, length);
}

static void hooked_midi_incoming(void *self, void *input, const void *message)
{
    const uint8_t *payload = 0;
    int length = 0;
    if (message) {
        memcpy(&payload, (const uint8_t *)message + 8u, sizeof(payload));
        memcpy(&length, (const uint8_t *)message + 12u, sizeof(length));
    }
    queue_record(RECORD_MIDI_HOST_TO_RX3, payload, length);
    original_midi_incoming(self, input, message);
}

static void line_char(struct line_buffer *line, char value)
{
    if (line->length >= sizeof(line->data)) {
        line->failed = 1u;
        return;
    }
    line->data[line->length++] = value;
}

static void line_text(struct line_buffer *line, const char *text)
{
    while (*text)
        line_char(line, *text++);
}

static void line_unsigned(struct line_buffer *line, uint32_t value)
{
    char digits[10];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof(digits));
    while (count)
        line_char(line, digits[--count]);
}

static void line_hex(struct line_buffer *line, const uint8_t *data,
                     unsigned int length)
{
    static const char digits[] = "0123456789abcdef";
    for (unsigned int index = 0; index < length; index++) {
        line_char(line, digits[data[index] >> 4u]);
        line_char(line, digits[data[index] & 0x0fu]);
    }
}

static const char *record_type(uint8_t kind)
{
    if (kind == RECORD_HID_HOST_TO_RX3)
        return "hid_host_to_rx3";
    if (kind == RECORD_HID_RX3_TO_HOST)
        return "hid_rx3_to_host";
    return "midi_host_to_rx3";
}

static int write_record(int fd, const struct capture_record *record)
{
    struct line_buffer line;
    struct timespec now;
    memset(&line, 0, sizeof(line));
    memset(&now, 0, sizeof(now));
    (void)clock_gettime(CLOCK_MONOTONIC, &now);

    line_text(&line, "{\"seq\":");
    line_unsigned(&line, record->sequence);
    line_text(&line, ",\"monotonicSec\":");
    line_unsigned(&line, (uint32_t)now.tv_sec);
    line_text(&line, ",\"monotonicNsec\":");
    line_unsigned(&line, (uint32_t)now.tv_nsec);
    line_text(&line, ",\"type\":\"");
    line_text(&line, record_type(record->kind));
    line_text(&line, "\",\"length\":");
    line_unsigned(&line, record->original_length);
    line_text(&line, ",\"capturedLength\":");
    line_unsigned(&line, record->captured_length);
    line_text(&line, ",\"truncated\":");
    line_text(&line, record->truncated ? "true" : "false");
    line_text(&line, ",\"hex\":\"");
    line_hex(&line, record->payload, record->captured_length);
    line_text(&line, "\"}\n");

    return !line.failed && write_all(fd, line.data, line.length);
}

static void write_dropped(int fd, unsigned int count)
{
    struct line_buffer line;
    memset(&line, 0, sizeof(line));
    line_text(&line, "{\"type\":\"dropped\",\"count\":");
    line_unsigned(&line, count);
    line_text(&line, "}\n");
    if (!line.failed)
        (void)write_all(fd, line.data, line.length);
}

static void *capture_worker(void *unused)
{
    (void)unused;
    const char *capture_path = getenv(CAPTURE_PATH_ENV);
    if (!capture_path || capture_path[0] != '/') {
        log_line("rejected: capture output path is missing");
        return 0;
    }
    int fd = open(capture_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        log_line("rejected: capture output could not be created");
        return 0;
    }
    static const char header[] =
        "{\"type\":\"capture_start\",\"firmware\":\"1.19\","
        "\"rbpSha1\":\"cf309238491e73cdbdc1f08a09f7a3177e079068\"}\n";
    if (!write_all(fd, header, sizeof(header) - 1u)) {
        close(fd);
        log_line("rejected: capture header could not be written");
        return 0;
    }
    publish_ready(capture_path);

    for (;;) {
        struct capture_record record;
        ssize_t count = recv(capture_socket[1], &record, sizeof(record), 0);
        if (count != (ssize_t)sizeof(record))
            continue;

        off_t size = lseek(fd, 0, SEEK_END);
        /* One record and a dropped-count record fit inside 2 KiB. Reserve
         * that space before either write so the advertised cap is exact. */
        if (size < 0 || (unsigned long)size + 2048u > CAPTURE_LIMIT) {
            static const char limit[] =
                "{\"type\":\"capture_stopped\",\"reason\":\"size_limit\"}\n";
            (void)write_all(fd, limit, sizeof(limit) - 1u);
            close(fd);
            log_line("capture stopped: 64 MiB limit reached");
            return 0;
        }

        unsigned int dropped = __sync_lock_test_and_set(&dropped_records, 0u);
        if (dropped)
            write_dropped(fd, dropped);
        if (!write_record(fd, &record)) {
            close(fd);
            log_line("capture stopped: output write failed");
            return 0;
        }
    }
}

__attribute__((constructor)) static void initialize(void)
{
    if (!running_in_rbp())
        return;
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, capture_socket)) {
        log_line("rejected: capture socket could not be created");
        return;
    }

    original_hid_incoming = (hid_message_fn)install_hook(
        &hid_incoming_hook, HID_INCOMING, hid_incoming_guard,
        (void *)hooked_hid_incoming);
    if (!original_hid_incoming) {
        log_line("rejected: inbound HID prologue mismatch");
        goto reject;
    }
    original_hid_outgoing = (hid_message_fn)install_hook(
        &hid_outgoing_hook, HID_OUTGOING, hid_outgoing_guard,
        (void *)hooked_hid_outgoing);
    if (!original_hid_outgoing) {
        log_line("rejected: outbound HID prologue mismatch");
        goto reject;
    }
    original_midi_incoming = (midi_message_fn)install_hook(
        &midi_incoming_hook, MIDI_INCOMING, midi_incoming_guard,
        (void *)hooked_midi_incoming);
    if (!original_midi_incoming) {
        log_line("rejected: inbound MIDI prologue mismatch");
        goto reject;
    }

    pthread_t thread;
    if (pthread_create(&thread, 0, capture_worker, 0)) {
        log_line("rejected: capture worker could not start");
        goto reject;
    }
    pthread_detach(thread);
    log_line("rekordbox USB capture hooks active");
    return;

reject:
    uninstall_hook(&midi_incoming_hook);
    uninstall_hook(&hid_outgoing_hook);
    uninstall_hook(&hid_incoming_hook);
    close(capture_socket[0]);
    close(capture_socket[1]);
    capture_socket[0] = -1;
    capture_socket[1] = -1;
}
