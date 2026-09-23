// SPDX-License-Identifier: MPL-2.0
/* Event-driven XDJ-RX3 1.19 deck telemetry transmitter.
 *
 * Guarded hooks follow the standalone player's track, status, unload, and
 * mixer on-air updates. They only copy load metadata, set an atomic bit, and
 * wake a worker. The worker owns every firmware accessor and HID write, and
 * also blocks on the gadget driver's sysfs connect notification. No deck
 * state is periodically polled.
 */

typedef unsigned int size_t;
typedef int ssize_t;
typedef long off_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long pthread_t;

struct pollfd {
    int fd;
    short events;
    short revents;
};

extern int open(const char *, int, ...);
extern ssize_t read(int, void *, size_t);
extern ssize_t write(int, const void *, size_t);
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
extern int poll(struct pollfd *, unsigned long, int);
extern int usleep(unsigned int);

#define O_RDONLY 0
#define O_WRONLY 1
#define O_NONBLOCK 04000
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_APPEND 02000
#define SEEK_SET 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void *)-1)
#define _SC_PAGESIZE 30
#define AF_UNIX 1
#define SOCK_DGRAM 2
#define SOCK_NONBLOCK 04000
#define MSG_DONTWAIT 0x40
#define POLLIN 0x0001
#define POLLPRI 0x0002
#define POLLERR 0x0008

#define HID_PATH "/dev/hidg0"
#define CONNECT_PATH "/sys/class/paudiog/paudiog0/connect"
#define READY_PATH "/tmp/rx3-usb-telemetry.ready"
#define LOG_PATH "/tmp/rx3-usb-telemetry.log"

#define REPORT_SIZE 20u
#define PAYLOAD_SIZE 12u
#define PROTOCOL_VERSION 1u
#define MESSAGE_HELLO 1u
#define MESSAGE_STATE 2u
#define MESSAGE_METADATA 3u
#define FIELD_TITLE 1u
#define FIELD_ARTIST 2u
#define FIELD_ALBUM 3u
#define FIELD_KEY 4u
#define BOTH_DECKS 3u

#define PLAYER_STATUS_UPDATED ((unsigned long)0x002f1bf8)
#define PLAYER_LOAD_TRACK ((unsigned long)0x002f20e4)
#define PLAYER_UNLOAD_RESULT ((unsigned long)0x002f1e4c)
#define MIXER_UPDATE_ON_AIR ((unsigned long)0x00057fb0)
#define GET_PLAY_MODE ((unsigned long)0x000fd960)
#define GET_PLAY_BPM ((unsigned long)0x000fd1fc)
#define GET_PLAY_TEMPO ((unsigned long)0x000fd2dc)
#define GET_MIXER_ON_AIR ((unsigned long)0x000fe34c)
#define PLAYER_CHANNEL_OFFSET 0x26u
#define MUSIC_TITLE_OFFSET 0x28u

typedef int (*get_int_fn)(unsigned int);
typedef int (*get_mixer_on_air_fn)(unsigned int, int);
typedef unsigned long (*player_status_fn)(void *, unsigned long);
typedef unsigned long (*player_load_fn)(void *, const void *);
typedef unsigned long (*player_unload_fn)(void *, unsigned long,
                                          unsigned long, unsigned long);
typedef unsigned long (*mixer_update_fn)(void *);

struct installed_hook {
    unsigned long address;
    uint8_t original[8];
    void *trampoline;
};

struct deck_cache {
    uint8_t valid;
    uint8_t generation;
    uint8_t state[PAYLOAD_SIZE];
    char title[128];
    char artist[128];
    char album[128];
    char key[32];
};

struct native_deck {
    volatile unsigned int revision;
    uint8_t loaded;
    uint32_t track_number;
    uint16_t title[128];
};

static const uint8_t player_status_guard[8] = {
    0xf8, 0x40, 0x2d, 0xe9, 0x01, 0x20, 0xa0, 0xe1
};
static const uint8_t player_load_guard[8] = {
    0xf0, 0x4f, 0x2d, 0xe9, 0x00, 0x40, 0xa0, 0xe1
};
static const uint8_t player_unload_guard[8] = {
    0xf8, 0x40, 0x2d, 0xe9, 0x00, 0x40, 0xa0, 0xe1
};
static const uint8_t mixer_update_guard[8] = {
    0xf8, 0x4f, 0x2d, 0xe9, 0x04, 0x8b, 0x2d, 0xed
};
static const uint8_t play_mode_guard[8] = {
    0xf8, 0x40, 0x2d, 0xe9, 0x00, 0x60, 0xa0, 0xe1
};
static const uint8_t play_bpm_guard[8] = {
    0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1
};
static const uint8_t play_tempo_guard[8] = {
    0x24, 0x30, 0x9f, 0xe5, 0x00, 0x10, 0xa0, 0xe1
};
static const uint8_t mixer_on_air_guard[8] = {
    0x01, 0x20, 0xa0, 0xe1, 0x30, 0x10, 0x9f, 0xe5
};

static struct installed_hook player_status_hook;
static struct installed_hook player_load_hook;
static struct installed_hook player_unload_hook;
static struct installed_hook mixer_update_hook;
static player_status_fn original_player_status;
static player_load_fn original_player_load;
static player_unload_fn original_player_unload;
static mixer_update_fn original_mixer_update;
static struct deck_cache decks[2];
static struct native_deck native_decks[2];
static volatile unsigned int pending_decks;
static int wake_socket[2] = {-1, -1};
static int hid_fd = -1;
static uint8_t sequence;

static void log_line(const char *text)
{
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return;
    size_t length = 0;
    while (text[length])
        length++;
    write(fd, text, length);
    write(fd, "\n", 1u);
    close(fd);
}

static void publish_ready(void)
{
    int fd = open(READY_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return;
    write(fd, "ready\n", 6u);
    close(fd);
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

static int accessor_guards_match(void)
{
    return memcmp((const void *)GET_PLAY_MODE, play_mode_guard, 8u) == 0 &&
           memcmp((const void *)GET_PLAY_BPM, play_bpm_guard, 8u) == 0 &&
           memcmp((const void *)GET_PLAY_TEMPO, play_tempo_guard, 8u) == 0 &&
           memcmp((const void *)GET_MIXER_ON_AIR,
                  mixer_on_air_guard, 8u) == 0;
}

static void signal_deck_change(unsigned int decks_changed)
{
    if (!decks_changed)
        return;
    unsigned int previous = __sync_fetch_and_or(&pending_decks, decks_changed);
    if (!previous && wake_socket[0] >= 0) {
        uint8_t wake = 1u;
        (void)send(wake_socket[0], &wake, sizeof(wake), MSG_DONTWAIT);
    }
}

static int player_index(void *player)
{
    unsigned int channel = *((const uint8_t *)player + PLAYER_CHANNEL_OFFSET);
    return channel >= 1u && channel <= 2u ? (int)channel - 1 : -1;
}

static unsigned int player_mask(void *player)
{
    int index = player_index(player);
    return index >= 0 ? 1u << (unsigned int)index : BOTH_DECKS;
}

static void begin_native_write(struct native_deck *deck)
{
    (void)__sync_add_and_fetch(&deck->revision, 1u);
}

static void finish_native_write(struct native_deck *deck)
{
    __sync_synchronize();
    (void)__sync_add_and_fetch(&deck->revision, 1u);
}

static unsigned long hooked_player_status(void *player, unsigned long force)
{
    unsigned long result = original_player_status(player, force);
    signal_deck_change(player_mask(player));
    return result;
}

static unsigned long hooked_player_load(void *player, const void *music_info)
{
    unsigned long result = original_player_load(player, music_info);
    int index = player_index(player);
    if (index >= 0 && music_info) {
        struct native_deck *deck = &native_decks[index];
        begin_native_write(deck);
        deck->loaded = 1u;
        memcpy(&deck->track_number, music_info, sizeof(deck->track_number));
        memcpy(deck->title,
               (const uint8_t *)music_info + MUSIC_TITLE_OFFSET,
               sizeof(deck->title));
        deck->title[(sizeof(deck->title) / sizeof(deck->title[0])) - 1u] = 0;
        finish_native_write(deck);
    }
    signal_deck_change(player_mask(player));
    return result;
}

static unsigned long hooked_player_unload(void *player, unsigned long result_code,
                                          unsigned long device,
                                          unsigned long device_number)
{
    unsigned long result = original_player_unload(
        player, result_code, device, device_number);
    int index = player_index(player);
    if (index >= 0) {
        struct native_deck *deck = &native_decks[index];
        begin_native_write(deck);
        deck->loaded = 0u;
        deck->track_number = 0u;
        memset(deck->title, 0, sizeof(deck->title));
        finish_native_write(deck);
    }
    signal_deck_change(player_mask(player));
    return result;
}

static unsigned long hooked_mixer_update(void *mixer)
{
    uint8_t before_a = *((const uint8_t *)mixer + 0x64u);
    uint8_t before_b = *((const uint8_t *)mixer + 0x65u);
    unsigned long result = original_mixer_update(mixer);
    uint8_t after_a = *((const uint8_t *)mixer + 0x64u);
    uint8_t after_b = *((const uint8_t *)mixer + 0x65u);
    if (before_a != after_a || before_b != after_b)
        signal_deck_change(BOTH_DECKS);
    return result;
}

static int read_connection(int fd)
{
    char state[16];
    if (lseek(fd, 0, SEEK_SET) < 0)
        return 0;
    ssize_t count = read(fd, state, sizeof(state));
    return count >= 7 && memcmp(state, "connect", 7u) == 0;
}

static void put_u16(uint8_t *target, unsigned int value)
{
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8u);
    target[2] = (uint8_t)(value >> 16u);
    target[3] = (uint8_t)(value >> 24u);
}

static int write_report(uint8_t type, uint8_t deck, const uint8_t *payload)
{
    uint8_t report[REPORT_SIZE];
    memset(report, 0, sizeof(report));
    report[0] = 'R';
    report[1] = 'X';
    report[2] = '3';
    report[3] = 'T';
    report[4] = PROTOCOL_VERSION;
    report[5] = type;
    report[6] = deck;
    report[7] = sequence;
    memcpy(report + 8u, payload, PAYLOAD_SIZE);

    /* hidg exposes a one-report queue.  A nonblocking startup burst accepts
     * the hello, then returns EAGAIN for the state and metadata behind it.
     * This worker is the sole writer and never runs in an rbp callback, so a
     * blocking descriptor provides the endpoint's intended backpressure. */
    if (hid_fd < 0)
        hid_fd = open(HID_PATH, O_WRONLY);
    if (hid_fd < 0)
        return 0;
    if (write(hid_fd, report, sizeof(report)) == (ssize_t)sizeof(report)) {
        sequence++;
        return 1;
    }
    close(hid_fd);
    hid_fd = -1;
    return 0;
}

static void send_hello(void)
{
    uint8_t payload[PAYLOAD_SIZE];
    memset(payload, 0, sizeof(payload));
    payload[0] = 2u;
    payload[1] = 0x03u;
    payload[4] = 0xcfu;
    payload[5] = 0x30u;
    payload[6] = 0x92u;
    payload[7] = 0x38u;
    write_report(MESSAGE_HELLO, 0u, payload);
}

static unsigned int bounded_length(const char *value, unsigned int capacity)
{
    unsigned int length = 0;
    while (length < capacity && value[length])
        length++;
    return length;
}

static int send_metadata(uint8_t deck, uint8_t field, uint8_t generation,
                         const char *value, unsigned int capacity)
{
    unsigned int length = bounded_length(value, capacity);
    unsigned int fragments = (length + 7u) / 8u;
    if (!fragments)
        fragments = 1u;
    for (unsigned int fragment = 0; fragment < fragments; fragment++) {
        uint8_t payload[PAYLOAD_SIZE];
        memset(payload, 0, sizeof(payload));
        payload[0] = field;
        payload[1] = generation;
        payload[2] = (uint8_t)fragment;
        payload[3] = (uint8_t)fragments;
        unsigned int start = fragment * 8u;
        unsigned int remaining = length > start ? length - start : 0u;
        unsigned int count = remaining < 8u ? remaining : 8u;
        if (count)
            memcpy(payload + 4u, value + start, count);
        if (!write_report(MESSAGE_METADATA, deck, payload))
            return 0;
    }
    return 1;
}

static unsigned int append_utf8(char *target, unsigned int written,
                                unsigned int capacity, uint32_t codepoint)
{
    uint8_t bytes[4];
    unsigned int count;
    if (codepoint <= 0x7fu) {
        bytes[0] = (uint8_t)codepoint;
        count = 1u;
    } else if (codepoint <= 0x7ffu) {
        bytes[0] = (uint8_t)(0xc0u | (codepoint >> 6u));
        bytes[1] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        count = 2u;
    } else if (codepoint <= 0xffffu) {
        bytes[0] = (uint8_t)(0xe0u | (codepoint >> 12u));
        bytes[1] = (uint8_t)(0x80u | ((codepoint >> 6u) & 0x3fu));
        bytes[2] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        count = 3u;
    } else {
        bytes[0] = (uint8_t)(0xf0u | (codepoint >> 18u));
        bytes[1] = (uint8_t)(0x80u | ((codepoint >> 12u) & 0x3fu));
        bytes[2] = (uint8_t)(0x80u | ((codepoint >> 6u) & 0x3fu));
        bytes[3] = (uint8_t)(0x80u | (codepoint & 0x3fu));
        count = 4u;
    }
    if (written + count >= capacity)
        return written;
    memcpy(target + written, bytes, count);
    return written + count;
}

static void utf16_to_utf8(const uint16_t *source, unsigned int units,
                          char *target, unsigned int capacity)
{
    memset(target, 0, capacity);

    unsigned int written = 0;
    for (unsigned int index = 0; index < units && source[index]; index++) {
        uint32_t codepoint = source[index];
        if (codepoint >= 0xd800u && codepoint <= 0xdbffu &&
            index + 1u < units && source[index + 1u] >= 0xdc00u &&
            source[index + 1u] <= 0xdfffu) {
            codepoint = 0x10000u + ((codepoint - 0xd800u) << 10u) +
                        (source[++index] - 0xdc00u);
        } else if (codepoint >= 0xd800u && codepoint <= 0xdfffu) {
            codepoint = 0xfffdu;
        }
        unsigned int next = append_utf8(target, written, capacity, codepoint);
        if (next == written)
            break;
        written = next;
    }
    target[written] = '\0';
}

static void read_native_deck(unsigned int index, uint8_t *loaded,
                             uint32_t *track_number, uint16_t title[128])
{
    struct native_deck *deck = &native_decks[index];
    for (;;) {
        unsigned int before = deck->revision;
        if (before & 1u)
            continue;
        __sync_synchronize();
        *loaded = deck->loaded;
        *track_number = deck->track_number;
        memcpy(title, deck->title, sizeof(deck->title));
        __sync_synchronize();
        unsigned int after = deck->revision;
        if (before == after && !(after & 1u))
            return;
    }
}

static void sample_deck(unsigned int index, int force)
{
    unsigned int deck = index + 1u;
    struct deck_cache *cache = &decks[index];
    char title[sizeof(cache->title)];
    char artist[sizeof(cache->artist)];
    char album[sizeof(cache->album)];
    char key[sizeof(cache->key)];
    uint16_t native_title[128];
    uint8_t loaded;
    uint32_t track_number;

    read_native_deck(index, &loaded, &track_number, native_title);
    memset(title, 0, sizeof(title));
    memset(artist, 0, sizeof(artist));
    memset(album, 0, sizeof(album));
    memset(key, 0, sizeof(key));
    if (loaded)
        utf16_to_utf8(native_title, 128u, title, sizeof(title));

    int metadata_changed = force || !cache->valid ||
        memcmp(title, cache->title, sizeof(title)) != 0 ||
        memcmp(artist, cache->artist, sizeof(artist)) != 0 ||
        memcmp(album, cache->album, sizeof(album)) != 0 ||
        memcmp(key, cache->key, sizeof(key)) != 0;
    uint8_t generation = cache->generation;
    if (metadata_changed)
        generation++;

    uint8_t state[PAYLOAD_SIZE];
    memset(state, 0, sizeof(state));
    int on_air = ((get_mixer_on_air_fn)GET_MIXER_ON_AIR)(index, 0);
    int play_state = ((get_int_fn)GET_PLAY_MODE)(index);
    int bpm = ((get_int_fn)GET_PLAY_BPM)(index);
    int tempo = ((get_int_fn)GET_PLAY_TEMPO)(index);
    state[0] = (loaded ? 0x01u : 0u) | (on_air ? 0x02u : 0u);
    state[1] = (uint8_t)play_state;
    state[2] = generation;
    put_u32(state + 4u, track_number);
    put_u16(state + 8u, bpm < 0 || bpm == 0xffff ? 0u : (unsigned int)bpm);
    put_u16(state + 10u, (unsigned int)tempo);

    int delivered = 1;
    if (force || !cache->valid || memcmp(state, cache->state, sizeof(state)))
        delivered = write_report(MESSAGE_STATE, (uint8_t)deck, state);
    if (metadata_changed) {
        delivered = delivered &&
            send_metadata((uint8_t)deck, FIELD_TITLE, generation,
                          title, sizeof(title));
        delivered = delivered &&
            send_metadata((uint8_t)deck, FIELD_ARTIST, generation,
                          artist, sizeof(artist));
        delivered = delivered &&
            send_metadata((uint8_t)deck, FIELD_ALBUM, generation,
                          album, sizeof(album));
        delivered = delivered &&
            send_metadata((uint8_t)deck, FIELD_KEY, generation,
                          key, sizeof(key));
    }
    if (!delivered)
        return;

    cache->generation = generation;
    if (metadata_changed) {
        memcpy(cache->title, title, sizeof(title));
        memcpy(cache->artist, artist, sizeof(artist));
        memcpy(cache->album, album, sizeof(album));
        memcpy(cache->key, key, sizeof(key));
    }
    memcpy(cache->state, state, sizeof(state));
    cache->valid = 1u;
}

static void close_transport(void)
{
    if (hid_fd >= 0)
        close(hid_fd);
    hid_fd = -1;
    memset(decks, 0, sizeof(decks));
}

static void drain_wake_socket(void)
{
    uint8_t bytes[32];
    while (recv(wake_socket[1], bytes, sizeof(bytes), MSG_DONTWAIT) > 0)
        ;
}

static void *telemetry_loop(void *unused)
{
    (void)unused;
    int connection_fd = open(CONNECT_PATH, O_RDONLY);
    if (connection_fd < 0) {
        log_line("worker stopped: USB connection state is unavailable");
        return 0;
    }

    int connected = read_connection(connection_fd);
    if (connected) {
        send_hello();
        sample_deck(0u, 1);
        sample_deck(1u, 1);
    }

    struct pollfd descriptors[2];
    descriptors[0].fd = wake_socket[1];
    descriptors[0].events = POLLIN;
    descriptors[1].fd = connection_fd;
    descriptors[1].events = POLLPRI | POLLERR;

    for (;;) {
        descriptors[0].revents = 0;
        descriptors[1].revents = 0;
        if (poll(descriptors, 2u, -1) < 0)
            continue;

        if (descriptors[1].revents & (POLLPRI | POLLERR)) {
            int next = read_connection(connection_fd);
            if (next && !connected) {
                connected = 1;
                send_hello();
                sample_deck(0u, 1);
                sample_deck(1u, 1);
            } else if (!next && connected) {
                connected = 0;
                close_transport();
            }
        }

        if (descriptors[0].revents & POLLIN) {
            drain_wake_socket();
            unsigned int changed = __sync_lock_test_and_set(&pending_decks, 0u);
            if (!connected || !changed)
                continue;
            usleep(10000u);
            if (changed & 1u)
                sample_deck(0u, 0);
            if (changed & 2u)
                sample_deck(1u, 0);
        }
    }
}

__attribute__((constructor)) static void initialize(void)
{
    if (!accessor_guards_match()) {
        log_line("rejected: telemetry accessor prologue mismatch");
        return;
    }
    if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, wake_socket)) {
        log_line("rejected: event socket could not be created");
        return;
    }

    original_player_status = (player_status_fn)install_hook(
        &player_status_hook, PLAYER_STATUS_UPDATED,
        player_status_guard, (void *)hooked_player_status);
    if (!original_player_status) {
        log_line("rejected: player status event prologue mismatch");
        goto reject;
    }
    original_player_load = (player_load_fn)install_hook(
        &player_load_hook, PLAYER_LOAD_TRACK,
        player_load_guard, (void *)hooked_player_load);
    if (!original_player_load) {
        log_line("rejected: player load event prologue mismatch");
        goto reject;
    }
    original_player_unload = (player_unload_fn)install_hook(
        &player_unload_hook, PLAYER_UNLOAD_RESULT,
        player_unload_guard, (void *)hooked_player_unload);
    if (!original_player_unload) {
        log_line("rejected: player unload event prologue mismatch");
        goto reject;
    }
    original_mixer_update = (mixer_update_fn)install_hook(
        &mixer_update_hook, MIXER_UPDATE_ON_AIR,
        mixer_update_guard, (void *)hooked_mixer_update);
    if (!original_mixer_update) {
        log_line("rejected: mixer on-air event prologue mismatch");
        goto reject;
    }

    pthread_t thread;
    if (pthread_create(&thread, 0, telemetry_loop, 0)) {
        log_line("rejected: telemetry worker could not start");
        goto reject;
    }
    pthread_detach(thread);
    publish_ready();
    log_line("USB telemetry prototype active: event-driven protocol v1");
    return;

reject:
    uninstall_hook(&mixer_update_hook);
    uninstall_hook(&player_unload_hook);
    uninstall_hook(&player_load_hook);
    uninstall_hook(&player_status_hook);
    close(wake_socket[0]);
    close(wake_socket[1]);
    wake_socket[0] = -1;
    wake_socket[1] = -1;
}
