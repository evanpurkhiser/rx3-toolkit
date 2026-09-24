// SPDX-License-Identifier: MPL-2.0
/* Event-driven XDJ-RX3 1.19 console tracer.
 *
 * Firmware callbacks only copy committed load data, set atomic reason bits,
 * and wake one worker. The worker owns all firmware accessors and JSONL I/O.
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

struct timespec {
    long tv_sec;
    long tv_nsec;
};

extern int open(const char *, int, ...);
extern ssize_t write(int, const void *, size_t);
extern ssize_t readlink(const char *, char *, size_t);
extern int close(int);
extern off_t lseek(int, off_t, int);
extern int ftruncate(int, off_t);
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
extern int clock_gettime(int, struct timespec *);

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
#define SOCK_NONBLOCK 04000
#define MSG_DONTWAIT 0x40
#define POLLIN 0x0001
#define CLOCK_MONOTONIC 1

#define EVENT_PATH "/dev/shm/rx3-events.jsonl"
#define READY_PATH "/tmp/rx3-event-tracer.ready"
#define LOG_PATH "/tmp/rx3-event-tracer.log"
#define EVENT_LIMIT (4u * 1024u * 1024u)
#define BOTH_DECKS 3u

#define REASON_STATUS 0x01u
#define REASON_LOAD 0x02u
#define REASON_UNLOAD 0x04u
#define REASON_MIXER 0x08u
#define REASON_INITIAL 0x10u

#define ACTION_STATE 1u
#define ACTION_PLAY 2u
#define ACTION_PAUSE 3u
#define ACTION_TEMPO 4u
#define ACTION_JOG_SPEED 5u
#define ACTION_JOG_TOUCH 6u
#define ACTION_JOG_PULSE 7u
#define ACTION_CUE_STANDBY 8u
#define ACTION_CUE_SCRATCH 9u
#define ACTION_LOOP_IN 10u
#define ACTION_LOOP_OUT 11u
#define ACTION_LOOP_EXIT 12u
#define ACTION_RELOOP 13u
#define ACTION_AUTO_LOOP 14u
#define ACTION_BEAT_JUMP 15u
#define ACTION_HOT_CUE_PLAY 16u
#define ACTION_HOT_CUE_RECORD 17u
#define ACTION_HOT_CUE_GATE 18u
#define ACTION_LINK_STATE 19u
#define ACTION_CONTROL 20u

#define PLAYER_STATUS_UPDATED ((unsigned long)0x002f1bf8)
#define PLAYER_LOAD_TRACK ((unsigned long)0x002f20e4)
#define PLAYER_UNLOAD_RESULT ((unsigned long)0x002f1e4c)
#define PLAYER_REF_CURRENT_TRACK ((unsigned long)0x002f1410)
#define MIXER_UPDATE_ON_AIR ((unsigned long)0x00057fb0)

#define GET_PLAY_TIME ((unsigned long)0x000fcf7c)
#define GET_PLAY_TOTAL_TIME ((unsigned long)0x000fcfa4)
#define GET_PLAY_SLIP_ON ((unsigned long)0x000fd014)
#define GET_PLAY_SLIPPING ((unsigned long)0x000fd044)
#define GET_PLAY_LOOP_IN_ADJUST ((unsigned long)0x000fd13c)
#define GET_PLAY_LOOP_OUT_ADJUST ((unsigned long)0x000fd16c)
#define GET_PLAY_JOG_TOUCHED ((unsigned long)0x000fd19c)
#define GET_PLAY_JOG_SCRATCHING ((unsigned long)0x000fd1cc)
#define GET_PLAY_BPM ((unsigned long)0x000fd1fc)
#define GET_PLAY_ORIGINAL_BPM ((unsigned long)0x000fd244)
#define GET_PLAY_TEMPO_BPM ((unsigned long)0x000fd28c)
#define GET_PLAY_TEMPO_RANGE ((unsigned long)0x000fd2b4)
#define GET_PLAY_TEMPO_RATE ((unsigned long)0x000fd2dc)
#define GET_PLAY_MASTER_TEMPO ((unsigned long)0x000fd30c)
#define GET_PLAY_AUTO_BEAT_LOOP ((unsigned long)0x000fd48c)
#define GET_PLAY_LOOP_BEAT_NUMER ((unsigned long)0x000fd4bc)
#define GET_PLAY_LOOP_BEAT_DENOM ((unsigned long)0x000fd4e4)
#define GET_CURRENT_CUE_EXIST ((unsigned long)0x000fd53c)
#define GET_CURRENT_CUE_IN ((unsigned long)0x000fd56c)
#define GET_CURRENT_CUE_OUT ((unsigned long)0x000fd5a4)
#define GET_CURRENT_CUE_LOOP ((unsigned long)0x000fd5dc)
#define GET_PLAY_MODE ((unsigned long)0x000fd960)
#define GET_MIXER_ON_AIR ((unsigned long)0x000fe34c)

#define GET_TITLE_STRING ((unsigned long)0x00125c30)
#define GET_ARTIST_STRING ((unsigned long)0x00125c60)
#define GET_ALBUM_STRING ((unsigned long)0x00125c90)
#define GET_KEY_STRING ((unsigned long)0x00125cf0)

#define ENGINE_PLAY ((unsigned long)0x000455b4)
#define ENGINE_PAUSE ((unsigned long)0x00045744)
#define ENGINE_TEMPO ((unsigned long)0x00045e6c)
#define ENGINE_JOG_SPEED ((unsigned long)0x00046d2c)
#define ENGINE_JOG_TOUCH ((unsigned long)0x00046de4)
#define ENGINE_JOG_PULSE ((unsigned long)0x0004736c)
#define ENGINE_CUE_STANDBY ((unsigned long)0x00047914)
#define ENGINE_CUE_SCRATCH ((unsigned long)0x000479cc)
#define ENGINE_LOOP_IN ((unsigned long)0x00047b3c)
#define ENGINE_LOOP_OUT ((unsigned long)0x00047bf4)
#define ENGINE_LOOP_EXIT ((unsigned long)0x000480c4)
#define ENGINE_RELOOP ((unsigned long)0x00048184)
#define ENGINE_HOT_CUE_RECORD ((unsigned long)0x000488c8)
#define ENGINE_HOT_CUE_PLAY ((unsigned long)0x00048980)
#define ENGINE_HOT_CUE_GATE ((unsigned long)0x00048c68)
#define ENGINE_AUTO_LOOP ((unsigned long)0x00048f40)
#define ENGINE_BEAT_JUMP ((unsigned long)0x00049ae0)
#define NETWORK_CHANGE_PLAY_STATUS ((unsigned long)0x0038f278)
#define KEY_MANAGER_SEND_KEY ((unsigned long)0x0037ad64)

#define PLAYER_CHANNEL_OFFSET 0x26u
#define MUSIC_ID_LOW_OFFSET 0x04u
#define MUSIC_TITLE_OFFSET 0x28u

typedef int (*get_int_fn)(unsigned int);
typedef int (*get_mixer_on_air_fn)(unsigned int, int);
typedef int (*get_string_fn)(unsigned int, uint16_t *, unsigned int);
typedef void (*player_status_fn)(void *, int);
typedef int (*player_load_fn)(void *, const void *);
typedef void (*player_unload_fn)(void *, int, unsigned int, unsigned int);
typedef void *(*player_ref_current_track_fn)(void *, int);
typedef void (*mixer_update_fn)(void *);
typedef int (*engine_four_arg_fn)(void *, unsigned int, int, int, int);
typedef int (*engine_value_fn)(void *, unsigned int, int);
typedef int (*engine_float_fn)(void *, unsigned int, float);
typedef int (*engine_two_bool_fn)(void *, unsigned int, int, int);
typedef int (*engine_simple_fn)(void *, unsigned int);
typedef int (*engine_auto_loop_fn)(void *, unsigned int, const void *, int, int, int);
typedef void (*network_status_fn)(void *, const void *);
typedef void (*key_manager_send_key_fn)(void *, int, int, int,
                                        long, float, long);

struct installed_hook {
    unsigned long address;
    uint8_t original[8];
    void *trampoline;
};

struct native_deck {
    volatile unsigned int revision;
    uint8_t loaded;
    uint32_t content_id;
    uint16_t title[128];
};

struct deck_state {
    uint8_t valid;
    uint8_t loaded;
    uint8_t on_air;
    uint8_t jog_touched;
    uint8_t jog_scratching;
    uint8_t master_tempo;
    uint8_t slip_on;
    uint8_t slipping;
    uint8_t loop_in_adjust;
    uint8_t loop_out_adjust;
    uint8_t auto_beat_loop;
    uint8_t cue_exists;
    uint8_t cue_is_loop;
    int play_mode;
    int play_time;
    int total_time;
    int bpm;
    int original_bpm;
    int tempo_bpm;
    int tempo_range;
    int tempo_rate;
    int loop_numer;
    int loop_denom;
    int cue_in;
    int cue_out;
    uint32_t content_id;
    char title[256];
    char artist[256];
    char album[256];
    char key[64];
};

struct json_buffer {
    char data[2048];
    unsigned int length;
    uint8_t failed;
};

struct code_guard {
    unsigned long address;
    uint8_t bytes[8];
};

struct action_record {
    uint8_t kind;
    uint8_t deck;
    uint8_t arg0;
    uint8_t arg1;
    int value0;
    int value1;
    int value2;
    int value3;
    uint32_t unsigned0;
    uint32_t unsigned1;
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

static const uint8_t engine_common_guard[8] = {
    0xf0, 0x41, 0x2d, 0xe9, 0x01, 0x50, 0xa0, 0xe1
};
static const uint8_t engine_wide_guard[8] = {
    0xf0, 0x47, 0x2d, 0xe9, 0x01, 0x50, 0xa0, 0xe1
};
static const uint8_t engine_simple_guard[8] = {
    0xf8, 0x40, 0x2d, 0xe9, 0x01, 0x50, 0xa0, 0xe1
};
static const uint8_t engine_auto_loop_guard[8] = {
    0xf0, 0x4f, 0x2d, 0xe9, 0x04, 0xd0, 0x4d, 0xe2
};
static const uint8_t network_status_guard[8] = {
    0xf0, 0x45, 0x2d, 0xe9, 0x28, 0x00, 0xa0, 0xe3
};
static const uint8_t key_manager_send_key_guard[8] = {
    0xf0, 0x4f, 0x2d, 0xe9, 0x0c, 0xd0, 0x4d, 0xe2
};

static const struct code_guard accessor_guards[] = {
    {PLAYER_REF_CURRENT_TRACK, {0x3c, 0x31, 0x90, 0xe5, 0x04, 0x00, 0x93, 0xe5}},
    {GET_PLAY_TIME, {0x1c, 0x30, 0x9f, 0xe5, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_TOTAL_TIME, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_SLIP_ON, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_SLIPPING, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_LOOP_IN_ADJUST, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_LOOP_OUT_ADJUST, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_JOG_TOUCHED, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_JOG_SCRATCHING, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_BPM, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_ORIGINAL_BPM, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_TEMPO_BPM, {0x1c, 0x30, 0x9f, 0xe5, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_TEMPO_RANGE, {0x1c, 0x30, 0x9f, 0xe5, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_TEMPO_RATE, {0x24, 0x30, 0x9f, 0xe5, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_MASTER_TEMPO, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_AUTO_BEAT_LOOP, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_LOOP_BEAT_NUMER, {0x1c, 0x30, 0x9f, 0xe5, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_LOOP_BEAT_DENOM, {0x1c, 0x30, 0x9f, 0xe5, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_CURRENT_CUE_EXIST, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_CURRENT_CUE_IN, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_CURRENT_CUE_OUT, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_CURRENT_CUE_LOOP, {0x08, 0x40, 0x2d, 0xe9, 0x00, 0x10, 0xa0, 0xe1}},
    {GET_PLAY_MODE, {0xf8, 0x40, 0x2d, 0xe9, 0x00, 0x60, 0xa0, 0xe1}},
    {GET_MIXER_ON_AIR, {0x01, 0x20, 0xa0, 0xe1, 0x30, 0x10, 0x9f, 0xe5}},
    {GET_TITLE_STRING, {0x01, 0x00, 0x40, 0xe2, 0x02, 0x30, 0xa0, 0xe1}},
    {GET_ARTIST_STRING, {0x01, 0x00, 0x40, 0xe2, 0x02, 0x30, 0xa0, 0xe1}},
    {GET_ALBUM_STRING, {0x01, 0x00, 0x40, 0xe2, 0x02, 0x30, 0xa0, 0xe1}},
    {GET_KEY_STRING, {0x01, 0x00, 0x40, 0xe2, 0x02, 0x30, 0xa0, 0xe1}},
};

static struct installed_hook player_status_hook;
static struct installed_hook player_load_hook;
static struct installed_hook player_unload_hook;
static struct installed_hook mixer_update_hook;
static struct installed_hook engine_play_hook;
static struct installed_hook engine_pause_hook;
static struct installed_hook engine_tempo_hook;
static struct installed_hook engine_jog_speed_hook;
static struct installed_hook engine_jog_touch_hook;
static struct installed_hook engine_jog_pulse_hook;
static struct installed_hook engine_cue_standby_hook;
static struct installed_hook engine_cue_scratch_hook;
static struct installed_hook engine_loop_in_hook;
static struct installed_hook engine_loop_out_hook;
static struct installed_hook engine_loop_exit_hook;
static struct installed_hook engine_reloop_hook;
static struct installed_hook engine_auto_loop_hook;
static struct installed_hook engine_beat_jump_hook;
static struct installed_hook engine_hot_cue_play_hook;
static struct installed_hook engine_hot_cue_record_hook;
static struct installed_hook engine_hot_cue_gate_hook;
static struct installed_hook network_status_hook;
static struct installed_hook key_manager_send_key_hook;
static player_status_fn original_player_status;
static player_load_fn original_player_load;
static player_unload_fn original_player_unload;
static mixer_update_fn original_mixer_update;
static engine_four_arg_fn original_engine_play;
static engine_four_arg_fn original_engine_pause;
static engine_float_fn original_engine_tempo;
static engine_float_fn original_engine_jog_speed;
static engine_value_fn original_engine_jog_touch;
static engine_value_fn original_engine_jog_pulse;
static engine_value_fn original_engine_cue_standby;
static engine_value_fn original_engine_cue_scratch;
static engine_value_fn original_engine_loop_in;
static engine_simple_fn original_engine_loop_out;
static engine_two_bool_fn original_engine_loop_exit;
static engine_simple_fn original_engine_reloop;
static engine_auto_loop_fn original_engine_auto_loop;
static engine_value_fn original_engine_beat_jump;
static engine_four_arg_fn original_engine_hot_cue_play;
static engine_value_fn original_engine_hot_cue_record;
static engine_value_fn original_engine_hot_cue_gate;
static network_status_fn original_network_status;
static key_manager_send_key_fn original_key_manager_send_key;
static struct native_deck native_decks[2];
static struct deck_state previous_states[2];
static volatile unsigned int pending_reasons[2];
static volatile unsigned int dropped_actions;
static int wake_socket[2] = {-1, -1};
static uint32_t event_sequence;

static void log_line(const char *text)
{
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return;
    size_t length = 0;
    while (text[length])
        length++;
    (void)write(fd, text, length);
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

static int reset_event_stream(void)
{
    int fd = open(EVENT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return 0;

    close(fd);
    return 1;
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
    unsigned int count = sizeof(accessor_guards) / sizeof(accessor_guards[0]);
    for (unsigned int index = 0; index < count; index++) {
        if (memcmp((const void *)accessor_guards[index].address,
                   accessor_guards[index].bytes, 8u))
            return 0;
    }
    return 1;
}

static int player_index(void *player)
{
    unsigned int channel = *((const uint8_t *)player + PLAYER_CHANNEL_OFFSET);
    return channel >= 1u && channel <= 2u ? (int)channel - 1 : -1;
}

static void queue_action(const struct action_record *action)
{
    if (wake_socket[0] < 0 ||
        send(wake_socket[0], action, sizeof(*action), MSG_DONTWAIT) !=
            (ssize_t)sizeof(*action))
        (void)__sync_add_and_fetch(&dropped_actions, 1u);
}

static void queue_engine_action(uint8_t kind, unsigned int channel,
                                int value0, int value1, int value2, int result)
{
    struct action_record action;
    memset(&action, 0, sizeof(action));
    action.kind = kind;
    action.deck = channel < 2u ? (uint8_t)(channel + 1u) : 0u;
    action.value0 = value0;
    action.value1 = value1;
    action.value2 = value2;
    action.value3 = result;
    queue_action(&action);
}

static int float_bits(float value)
{
    int bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void hooked_key_manager_send_key(void *manager, int key_code,
                                        int operation, int channel,
                                        long value, float float_value,
                                        long auxiliary)
{
    struct action_record action;
    memset(&action, 0, sizeof(action));
    action.kind = ACTION_CONTROL;
    action.value0 = key_code;
    action.value1 = operation;
    action.value2 = channel;
    action.value3 = (int)value;
    action.unsigned0 = (uint32_t)float_bits(float_value);
    action.unsigned1 = (uint32_t)auxiliary;
    queue_action(&action);

    original_key_manager_send_key(manager, key_code, operation, channel,
                                  value, float_value, auxiliary);
}

static void signal_deck(unsigned int index, unsigned int reason)
{
    if (index >= 2u)
        return;
    unsigned int previous = __sync_fetch_and_or(&pending_reasons[index], reason);
    if (!previous && wake_socket[0] >= 0) {
        struct action_record action;
        memset(&action, 0, sizeof(action));
        action.kind = ACTION_STATE;
        action.deck = (uint8_t)(index + 1u);
        queue_action(&action);
    }
}

static void signal_mask(unsigned int mask, unsigned int reason)
{
    if (mask & 1u)
        signal_deck(0u, reason);
    if (mask & 2u)
        signal_deck(1u, reason);
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

static void hooked_player_status(void *player, int force)
{
    original_player_status(player, force);
    int index = player_index(player);
    if (index >= 0)
        signal_deck((unsigned int)index, REASON_STATUS);
}

static int hooked_player_load(void *player, const void *music_info)
{
    int result = original_player_load(player, music_info);
    int index = player_index(player);
    if (result && index >= 0 && music_info) {
        struct native_deck *deck = &native_decks[index];
        begin_native_write(deck);
        memcpy(&deck->content_id,
               (const uint8_t *)music_info + MUSIC_ID_LOW_OFFSET,
               sizeof(deck->content_id));
        deck->loaded = deck->content_id != 0u;
        if (deck->loaded)
            memcpy(deck->title,
                   (const uint8_t *)music_info + MUSIC_TITLE_OFFSET,
                   sizeof(deck->title));
        else
            memset(deck->title, 0, sizeof(deck->title));
        deck->title[127] = 0;
        finish_native_write(deck);
        signal_deck((unsigned int)index, REASON_LOAD);
    }
    return result;
}

static void hooked_player_unload(void *player, int result_code,
                                 unsigned int device,
                                 unsigned int device_number)
{
    original_player_unload(player, result_code, device, device_number);
    int index = player_index(player);
    void *current = ((player_ref_current_track_fn)PLAYER_REF_CURRENT_TRACK)(player, 0);
    if (index >= 0 && !current) {
        struct native_deck *deck = &native_decks[index];
        begin_native_write(deck);
        deck->loaded = 0u;
        deck->content_id = 0u;
        memset(deck->title, 0, sizeof(deck->title));
        finish_native_write(deck);
        signal_deck((unsigned int)index, REASON_UNLOAD);
    }
}

static void hooked_mixer_update(void *mixer)
{
    uint8_t before_a = *((const uint8_t *)mixer + 0x64u);
    uint8_t before_b = *((const uint8_t *)mixer + 0x65u);
    original_mixer_update(mixer);
    uint8_t after_a = *((const uint8_t *)mixer + 0x64u);
    uint8_t after_b = *((const uint8_t *)mixer + 0x65u);
    if (before_a != after_a || before_b != after_b)
        signal_mask(BOTH_DECKS, REASON_MIXER);
}

static int hooked_engine_play(void *engine, unsigned int channel, int first,
                              int second, int third)
{
    int result = original_engine_play(engine, channel, first, second, third);
    queue_engine_action(ACTION_PLAY, channel, first, second, third, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_pause(void *engine, unsigned int channel, int first,
                               int second, int third)
{
    int result = original_engine_pause(engine, channel, first, second, third);
    queue_engine_action(ACTION_PAUSE, channel, first, second, third, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_tempo(void *engine, unsigned int channel, float value)
{
    int result = original_engine_tempo(engine, channel, value);
    queue_engine_action(ACTION_TEMPO, channel, float_bits(value), 0, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_jog_speed(void *engine, unsigned int channel, float value)
{
    int result = original_engine_jog_speed(engine, channel, value);
    queue_engine_action(ACTION_JOG_SPEED, channel, float_bits(value), 0, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_jog_touch(void *engine, unsigned int channel, int touched)
{
    int result = original_engine_jog_touch(engine, channel, touched);
    queue_engine_action(ACTION_JOG_TOUCH, channel, touched, 0, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_jog_pulse(void *engine, unsigned int channel, int pulse)
{
    int result = original_engine_jog_pulse(engine, channel, pulse);
    queue_engine_action(ACTION_JOG_PULSE, channel, pulse, 0, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_cue_standby(void *engine, unsigned int channel, int value)
{
    int result = original_engine_cue_standby(engine, channel, value);
    queue_engine_action(ACTION_CUE_STANDBY, channel, value, 0, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_cue_scratch(void *engine, unsigned int channel, int value)
{
    int result = original_engine_cue_scratch(engine, channel, value);
    queue_engine_action(ACTION_CUE_SCRATCH, channel, value, 0, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_loop_in(void *engine, unsigned int channel, int quantize)
{
    int result = original_engine_loop_in(engine, channel, quantize);
    queue_engine_action(ACTION_LOOP_IN, channel, quantize, 0, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_loop_out(void *engine, unsigned int channel)
{
    int result = original_engine_loop_out(engine, channel);
    queue_engine_action(ACTION_LOOP_OUT, channel, 0, 0, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_loop_exit(void *engine, unsigned int channel,
                                   int first, int second)
{
    int result = original_engine_loop_exit(engine, channel, first, second);
    queue_engine_action(ACTION_LOOP_EXIT, channel, first, second, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_reloop(void *engine, unsigned int channel)
{
    int result = original_engine_reloop(engine, channel);
    queue_engine_action(ACTION_RELOOP, channel, 0, 0, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_auto_loop(void *engine, unsigned int channel,
                                   const void *spec, int option,
                                   int first, int second)
{
    uint16_t numerator = 0;
    uint16_t denominator = 0;
    if (spec) {
        memcpy(&numerator, spec, sizeof(numerator));
        memcpy(&denominator, (const uint8_t *)spec + 2u, sizeof(denominator));
    }
    int result = original_engine_auto_loop(
        engine, channel, spec, option, first, second);
    queue_engine_action(ACTION_AUTO_LOOP, channel, numerator, denominator,
                        option, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_beat_jump(void *engine, unsigned int channel, int type)
{
    int result = original_engine_beat_jump(engine, channel, type);
    queue_engine_action(ACTION_BEAT_JUMP, channel, type, 0, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_hot_cue_play(void *engine, unsigned int channel,
                                      int cue, int first, int second)
{
    int result = original_engine_hot_cue_play(
        engine, channel, cue, first, second);
    queue_engine_action(ACTION_HOT_CUE_PLAY, channel, cue, first, second, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_hot_cue_record(void *engine, unsigned int channel, int cue)
{
    int result = original_engine_hot_cue_record(engine, channel, cue);
    queue_engine_action(ACTION_HOT_CUE_RECORD, channel, cue, 0, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static int hooked_engine_hot_cue_gate(void *engine, unsigned int channel, int cue)
{
    int result = original_engine_hot_cue_gate(engine, channel, cue);
    queue_engine_action(ACTION_HOT_CUE_GATE, channel, cue, 0, 0, result);
    signal_deck(channel, REASON_STATUS);
    return result;
}

static void hooked_network_status(void *network, const void *payload)
{
    original_network_status(network, payload);
    if (!payload)
        return;
    for (unsigned int index = 0; index < 2u; index++) {
        const uint8_t *record = (const uint8_t *)payload + index * 0x14u;
        struct action_record action;
        memset(&action, 0, sizeof(action));
        action.kind = ACTION_LINK_STATE;
        action.deck = (uint8_t)(index + 1u);
        action.arg0 = record[0];
        action.arg1 = record[1];
        memcpy(&action.unsigned0, record + 4u, sizeof(action.unsigned0));
        memcpy(&action.unsigned1, record + 8u, sizeof(action.unsigned1));
        memcpy(&action.value0, record + 0x0cu, sizeof(action.value0));
        action.value1 = record[0x10u];
        queue_action(&action);
    }
}

static void read_native_deck(unsigned int index, uint8_t *loaded,
                             uint32_t *content_id, uint16_t title[128])
{
    struct native_deck *deck = &native_decks[index];
    for (;;) {
        unsigned int before = deck->revision;
        if (before & 1u)
            continue;
        __sync_synchronize();
        *loaded = deck->loaded;
        *content_id = deck->content_id;
        memcpy(title, deck->title, sizeof(deck->title));
        __sync_synchronize();
        unsigned int after = deck->revision;
        if (before == after && !(after & 1u))
            return;
    }
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

static void read_track_string(unsigned long address, unsigned int deck,
                              char *target, unsigned int capacity)
{
    uint16_t native[128];
    memset(native, 0, sizeof(native));
    ((get_string_fn)address)(deck + 1u, native, sizeof(native));
    native[127] = 0;
    utf16_to_utf8(native, 128u, target, capacity);
}

static void sample_state(unsigned int index, struct deck_state *state)
{
    uint16_t native_title[128];
    uint8_t native_loaded;
    memset(state, 0, sizeof(*state));
    read_native_deck(index, &native_loaded, &state->content_id, native_title);

    read_track_string(GET_TITLE_STRING, index, state->title, sizeof(state->title));
    read_track_string(GET_ARTIST_STRING, index, state->artist, sizeof(state->artist));
    read_track_string(GET_ALBUM_STRING, index, state->album, sizeof(state->album));
    read_track_string(GET_KEY_STRING, index, state->key, sizeof(state->key));
    if (!state->title[0] && native_title[0])
        utf16_to_utf8(native_title, 128u, state->title, sizeof(state->title));
    state->loaded = native_loaded || state->title[0] || state->artist[0];

    state->play_mode = ((get_int_fn)GET_PLAY_MODE)(index);
    state->play_time = ((get_int_fn)GET_PLAY_TIME)(index);
    state->total_time = ((get_int_fn)GET_PLAY_TOTAL_TIME)(index);
    state->jog_touched = ((get_int_fn)GET_PLAY_JOG_TOUCHED)(index) != 0;
    state->jog_scratching = ((get_int_fn)GET_PLAY_JOG_SCRATCHING)(index) != 0;
    state->bpm = ((get_int_fn)GET_PLAY_BPM)(index);
    state->original_bpm = ((get_int_fn)GET_PLAY_ORIGINAL_BPM)(index);
    state->tempo_bpm = ((get_int_fn)GET_PLAY_TEMPO_BPM)(index);
    state->tempo_range = ((get_int_fn)GET_PLAY_TEMPO_RANGE)(index);
    state->tempo_rate = ((get_int_fn)GET_PLAY_TEMPO_RATE)(index);
    state->master_tempo = ((get_int_fn)GET_PLAY_MASTER_TEMPO)(index) != 0;
    state->slip_on = ((get_int_fn)GET_PLAY_SLIP_ON)(index) != 0;
    state->slipping = ((get_int_fn)GET_PLAY_SLIPPING)(index) != 0;
    state->loop_in_adjust = ((get_int_fn)GET_PLAY_LOOP_IN_ADJUST)(index) != 0;
    state->loop_out_adjust = ((get_int_fn)GET_PLAY_LOOP_OUT_ADJUST)(index) != 0;
    state->auto_beat_loop = ((get_int_fn)GET_PLAY_AUTO_BEAT_LOOP)(index) != 0;
    state->loop_numer = ((get_int_fn)GET_PLAY_LOOP_BEAT_NUMER)(index);
    state->loop_denom = ((get_int_fn)GET_PLAY_LOOP_BEAT_DENOM)(index);
    state->cue_exists = ((get_int_fn)GET_CURRENT_CUE_EXIST)(index) != 0;
    state->cue_in = ((get_int_fn)GET_CURRENT_CUE_IN)(index);
    state->cue_out = ((get_int_fn)GET_CURRENT_CUE_OUT)(index);
    state->cue_is_loop = ((get_int_fn)GET_CURRENT_CUE_LOOP)(index) != 0;
    state->on_air = ((get_mixer_on_air_fn)GET_MIXER_ON_AIR)(index, 0) != 0;
    state->valid = 1u;
}

static void json_char(struct json_buffer *json, char value)
{
    if (json->length + 1u >= sizeof(json->data)) {
        json->failed = 1u;
        return;
    }
    json->data[json->length++] = value;
}

static void json_text(struct json_buffer *json, const char *value)
{
    while (*value)
        json_char(json, *value++);
}

static void json_unsigned(struct json_buffer *json, uint32_t value)
{
    char digits[10];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof(digits));
    while (count)
        json_char(json, digits[--count]);
}

static void json_signed(struct json_buffer *json, int value)
{
    if (value < 0) {
        json_char(json, '-');
        json_unsigned(json, 0u - (uint32_t)value);
    } else {
        json_unsigned(json, (uint32_t)value);
    }
}

static void json_bool(struct json_buffer *json, int value)
{
    json_text(json, value ? "true" : "false");
}

static void json_string(struct json_buffer *json, const char *value)
{
    static const char hex[] = "0123456789abcdef";
    json_char(json, '"');
    while (*value) {
        uint8_t byte = (uint8_t)*value++;
        if (byte == '"' || byte == '\\') {
            json_char(json, '\\');
            json_char(json, (char)byte);
        } else if (byte < 0x20u) {
            json_text(json, "\\u00");
            json_char(json, hex[byte >> 4u]);
            json_char(json, hex[byte & 0x0fu]);
        } else {
            json_char(json, (char)byte);
        }
    }
    json_char(json, '"');
}

static void json_prefix(struct json_buffer *json, const char *type,
                        unsigned int deck, unsigned int reasons)
{
    struct timespec now;
    memset(json, 0, sizeof(*json));
    memset(&now, 0, sizeof(now));
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    json_text(json, "{\"seq\":");
    json_unsigned(json, event_sequence++);
    json_text(json, ",\"monotonicMs\":");
    json_unsigned(json, (uint32_t)(now.tv_sec * 1000l + now.tv_nsec / 1000000l));
    json_text(json, ",\"type\":");
    json_string(json, type);
    if (deck) {
        json_text(json, ",\"deck\":");
        json_unsigned(json, deck);
    }
    json_text(json, ",\"reasons\":");
    json_unsigned(json, reasons);
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

static void write_json(struct json_buffer *json)
{
    if (json->failed)
        return;
    json_char(json, '}');
    json_char(json, '\n');
    if (json->failed)
        return;

    int fd = open(EVENT_PATH, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0)
        return;
    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0 || (unsigned long)size + json->length > EVENT_LIMIT) {
        static const char reset[] = "{\"type\":\"stream_reset\",\"reason\":\"size_limit\"}\n";
        if (!ftruncate(fd, 0))
            (void)write_all(fd, reset, sizeof(reset) - 1u);
    }
    (void)write_all(fd, json->data, json->length);
    close(fd);
}

static int track_changed(const struct deck_state *before,
                         const struct deck_state *after)
{
    return !before->valid || before->loaded != after->loaded ||
           before->content_id != after->content_id ||
           memcmp(before->title, after->title, sizeof(after->title)) ||
           memcmp(before->artist, after->artist, sizeof(after->artist)) ||
           memcmp(before->album, after->album, sizeof(after->album)) ||
           memcmp(before->key, after->key, sizeof(after->key));
}

static void emit_track(unsigned int deck, unsigned int reasons,
                       const struct deck_state *state)
{
    struct json_buffer json;
    json_prefix(&json, state->loaded ? "track_load" : "track_unload", deck, reasons);
    json_text(&json, ",\"loaded\":");
    json_bool(&json, state->loaded);
    json_text(&json, ",\"contentId\":");
    json_unsigned(&json, state->content_id);
    json_text(&json, ",\"title\":");
    json_string(&json, state->title);
    json_text(&json, ",\"artist\":");
    json_string(&json, state->artist);
    json_text(&json, ",\"album\":");
    json_string(&json, state->album);
    json_text(&json, ",\"key\":");
    json_string(&json, state->key);
    write_json(&json);
}

static void emit_play(unsigned int deck, unsigned int reasons,
                      const struct deck_state *state)
{
    struct json_buffer json;
    json_prefix(&json, "play_state", deck, reasons);
    json_text(&json, ",\"rawMode\":");
    json_signed(&json, state->play_mode);
    write_json(&json);
}

static void emit_position(unsigned int deck, unsigned int reasons,
                          const struct deck_state *state)
{
    struct json_buffer json;
    json_prefix(&json, "position", deck, reasons);
    json_text(&json, ",\"timeRaw\":");
    json_signed(&json, state->play_time);
    json_text(&json, ",\"totalRaw\":");
    json_signed(&json, state->total_time);
    write_json(&json);
}

static void emit_jog(unsigned int deck, unsigned int reasons,
                     const struct deck_state *state)
{
    struct json_buffer json;
    json_prefix(&json, "jog", deck, reasons);
    json_text(&json, ",\"touched\":");
    json_bool(&json, state->jog_touched);
    json_text(&json, ",\"scratching\":");
    json_bool(&json, state->jog_scratching);
    write_json(&json);
}

static void emit_pitch(unsigned int deck, unsigned int reasons,
                       const struct deck_state *state)
{
    struct json_buffer json;
    json_prefix(&json, "pitch", deck, reasons);
    json_text(&json, ",\"tempoRaw\":");
    json_signed(&json, state->tempo_rate);
    json_text(&json, ",\"bpmRaw\":");
    json_signed(&json, state->bpm);
    json_text(&json, ",\"originalBpmRaw\":");
    json_signed(&json, state->original_bpm);
    json_text(&json, ",\"tempoBpmRaw\":");
    json_signed(&json, state->tempo_bpm);
    json_text(&json, ",\"rangeRaw\":");
    json_signed(&json, state->tempo_range);
    json_text(&json, ",\"masterTempo\":");
    json_bool(&json, state->master_tempo);
    write_json(&json);
}

static void emit_cue(unsigned int deck, unsigned int reasons,
                     const struct deck_state *state)
{
    struct json_buffer json;
    json_prefix(&json, "cue", deck, reasons);
    json_text(&json, ",\"exists\":");
    json_bool(&json, state->cue_exists);
    json_text(&json, ",\"inRaw\":");
    json_signed(&json, state->cue_in);
    json_text(&json, ",\"outRaw\":");
    json_signed(&json, state->cue_out);
    json_text(&json, ",\"isLoop\":");
    json_bool(&json, state->cue_is_loop);
    write_json(&json);
}

static void emit_loop(unsigned int deck, unsigned int reasons,
                      const struct deck_state *state)
{
    struct json_buffer json;
    json_prefix(&json, "loop", deck, reasons);
    json_text(&json, ",\"active\":");
    json_bool(&json, state->cue_is_loop || state->auto_beat_loop);
    json_text(&json, ",\"autoBeat\":");
    json_bool(&json, state->auto_beat_loop);
    json_text(&json, ",\"beatNumeratorRaw\":");
    json_signed(&json, state->loop_numer);
    json_text(&json, ",\"beatDenominatorRaw\":");
    json_signed(&json, state->loop_denom);
    json_text(&json, ",\"inAdjust\":");
    json_bool(&json, state->loop_in_adjust);
    json_text(&json, ",\"outAdjust\":");
    json_bool(&json, state->loop_out_adjust);
    write_json(&json);
}

static void emit_slip(unsigned int deck, unsigned int reasons,
                      const struct deck_state *state)
{
    struct json_buffer json;
    json_prefix(&json, "slip", deck, reasons);
    json_text(&json, ",\"enabled\":");
    json_bool(&json, state->slip_on);
    json_text(&json, ",\"active\":");
    json_bool(&json, state->slipping);
    write_json(&json);
}

static void emit_on_air(unsigned int deck, unsigned int reasons,
                        const struct deck_state *state)
{
    struct json_buffer json;
    json_prefix(&json, "on_air", deck, reasons);
    json_text(&json, ",\"active\":");
    json_bool(&json, state->on_air);
    write_json(&json);
}

static void publish_changes(unsigned int index, unsigned int reasons)
{
    struct deck_state next;
    struct deck_state *before = &previous_states[index];
    unsigned int deck = index + 1u;
    sample_state(index, &next);

    if (track_changed(before, &next))
        emit_track(deck, reasons, &next);
    if (!before->valid || before->play_mode != next.play_mode)
        emit_play(deck, reasons, &next);
    if (!before->valid || before->play_time != next.play_time ||
        before->total_time != next.total_time)
        emit_position(deck, reasons, &next);
    if (!before->valid || before->jog_touched != next.jog_touched ||
        before->jog_scratching != next.jog_scratching)
        emit_jog(deck, reasons, &next);
    if (!before->valid || before->bpm != next.bpm ||
        before->original_bpm != next.original_bpm ||
        before->tempo_bpm != next.tempo_bpm ||
        before->tempo_range != next.tempo_range ||
        before->tempo_rate != next.tempo_rate ||
        before->master_tempo != next.master_tempo)
        emit_pitch(deck, reasons, &next);
    if (!before->valid || before->cue_exists != next.cue_exists ||
        before->cue_in != next.cue_in || before->cue_out != next.cue_out ||
        before->cue_is_loop != next.cue_is_loop)
        emit_cue(deck, reasons, &next);
    if (!before->valid || before->cue_is_loop != next.cue_is_loop ||
        before->auto_beat_loop != next.auto_beat_loop ||
        before->loop_numer != next.loop_numer ||
        before->loop_denom != next.loop_denom ||
        before->loop_in_adjust != next.loop_in_adjust ||
        before->loop_out_adjust != next.loop_out_adjust)
        emit_loop(deck, reasons, &next);
    if (!before->valid || before->slip_on != next.slip_on ||
        before->slipping != next.slipping)
        emit_slip(deck, reasons, &next);
    if (!before->valid || before->on_air != next.on_air)
        emit_on_air(deck, reasons, &next);

    memcpy(before, &next, sizeof(*before));
}

static const char *action_name(uint8_t kind)
{
    switch (kind) {
    case ACTION_PLAY: return "play";
    case ACTION_PAUSE: return "pause";
    case ACTION_TEMPO: return "tempo_slider";
    case ACTION_JOG_SPEED: return "jog_speed";
    case ACTION_JOG_TOUCH: return "jog_touch";
    case ACTION_JOG_PULSE: return "jog_pulse";
    case ACTION_CUE_STANDBY: return "back_cue_standby";
    case ACTION_CUE_SCRATCH: return "back_cue_scratch";
    case ACTION_LOOP_IN: return "loop_in";
    case ACTION_LOOP_OUT: return "loop_out";
    case ACTION_LOOP_EXIT: return "loop_exit";
    case ACTION_RELOOP: return "reloop";
    case ACTION_AUTO_LOOP: return "auto_loop";
    case ACTION_BEAT_JUMP: return "beat_jump";
    case ACTION_HOT_CUE_PLAY: return "hot_cue_play";
    case ACTION_HOT_CUE_RECORD: return "hot_cue_record";
    case ACTION_HOT_CUE_GATE: return "hot_cue_gate";
    default: return "unknown";
    }
}

static void emit_action(const struct action_record *action)
{
    struct json_buffer json;
    json_prefix(&json, "action", action->deck, 0u);
    json_text(&json, ",\"name\":");
    json_string(&json, action_name(action->kind));
    json_text(&json, ",\"value0Raw\":");
    json_signed(&json, action->value0);
    json_text(&json, ",\"value1Raw\":");
    json_signed(&json, action->value1);
    json_text(&json, ",\"value2Raw\":");
    json_signed(&json, action->value2);
    json_text(&json, ",\"accepted\":");
    json_bool(&json, action->value3);
    write_json(&json);
}

static void emit_link_state(const struct action_record *action)
{
    struct json_buffer json;
    json_prefix(&json, "link_state", action->deck, 0u);
    json_text(&json, ",\"flags0Raw\":");
    json_unsigned(&json, action->arg0);
    json_text(&json, ",\"flags1Raw\":");
    json_unsigned(&json, action->arg1);
    json_text(&json, ",\"musicIdLow\":");
    json_unsigned(&json, action->unsigned0);
    json_text(&json, ",\"musicIdHigh\":");
    json_unsigned(&json, action->unsigned1);
    json_text(&json, ",\"statusRaw\":");
    json_signed(&json, action->value0);
    json_text(&json, ",\"tailRaw\":");
    json_signed(&json, action->value1);
    write_json(&json);
}

static void emit_control(const struct action_record *action)
{
    struct json_buffer json;
    json_prefix(&json, "control", 0u, 0u);
    json_text(&json, ",\"keyCode\":");
    json_signed(&json, action->value0);
    json_text(&json, ",\"operation\":");
    json_signed(&json, action->value1);
    json_text(&json, ",\"channel\":");
    json_signed(&json, action->value2);
    json_text(&json, ",\"valueRaw\":");
    json_signed(&json, action->value3);
    json_text(&json, ",\"floatRawBits\":");
    json_unsigned(&json, action->unsigned0);
    json_text(&json, ",\"auxRaw\":");
    json_signed(&json, (int)action->unsigned1);
    write_json(&json);
}

static void emit_dropped(unsigned int count)
{
    struct json_buffer json;
    json_prefix(&json, "dropped", 0u, 0u);
    json_text(&json, ",\"count\":");
    json_unsigned(&json, count);
    write_json(&json);
}

static void process_action(const struct action_record *action)
{
    if (action->kind == ACTION_STATE)
        return;
    if (action->kind == ACTION_LINK_STATE) {
        emit_link_state(action);
        return;
    }
    if (action->kind == ACTION_CONTROL) {
        emit_control(action);
        return;
    }
    emit_action(action);
}

static void *event_loop(void *unused)
{
    (void)unused;
    if (!reset_event_stream()) {
        log_line("rejected: event stream could not be created");
        return 0;
    }
    struct json_buffer json;
    json_prefix(&json, "ready", 0u, REASON_INITIAL);
    json_text(&json, ",\"firmware\":\"1.19\",\"rbpSha1\":\"cf309238491e73cdbdc1f08a09f7a3177e079068\"");
    write_json(&json);
    publish_changes(0u, REASON_INITIAL);
    publish_changes(1u, REASON_INITIAL);
    publish_ready();

    struct pollfd descriptor;
    descriptor.fd = wake_socket[1];
    descriptor.events = POLLIN;
    for (;;) {
        descriptor.revents = 0;
        if (poll(&descriptor, 1u, -1) < 0)
            continue;
        if (!(descriptor.revents & POLLIN))
            continue;
        struct action_record action;
        while (recv(wake_socket[1], &action, sizeof(action), MSG_DONTWAIT) ==
               (ssize_t)sizeof(action))
            process_action(&action);
        unsigned int reason_a = __sync_lock_test_and_set(&pending_reasons[0], 0u);
        unsigned int reason_b = __sync_lock_test_and_set(&pending_reasons[1], 0u);
        if (reason_a)
            publish_changes(0u, reason_a);
        if (reason_b)
            publish_changes(1u, reason_b);
        unsigned int dropped = __sync_lock_test_and_set(&dropped_actions, 0u);
        if (dropped)
            emit_dropped(dropped);
    }
}

__attribute__((constructor)) static void initialize(void)
{
    if (!running_in_rbp())
        return;

    if (!accessor_guards_match()) {
        log_line("rejected: event tracer accessor prologue mismatch");
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
    original_engine_play = (engine_four_arg_fn)install_hook(
        &engine_play_hook, ENGINE_PLAY, engine_wide_guard,
        (void *)hooked_engine_play);
    if (!original_engine_play)
        goto action_reject;
    original_engine_pause = (engine_four_arg_fn)install_hook(
        &engine_pause_hook, ENGINE_PAUSE, engine_wide_guard,
        (void *)hooked_engine_pause);
    if (!original_engine_pause)
        goto action_reject;
    original_engine_tempo = (engine_float_fn)install_hook(
        &engine_tempo_hook, ENGINE_TEMPO, engine_common_guard,
        (void *)hooked_engine_tempo);
    if (!original_engine_tempo)
        goto action_reject;
    original_engine_jog_speed = (engine_float_fn)install_hook(
        &engine_jog_speed_hook, ENGINE_JOG_SPEED, engine_common_guard,
        (void *)hooked_engine_jog_speed);
    if (!original_engine_jog_speed)
        goto action_reject;
    original_engine_jog_touch = (engine_value_fn)install_hook(
        &engine_jog_touch_hook, ENGINE_JOG_TOUCH, engine_common_guard,
        (void *)hooked_engine_jog_touch);
    if (!original_engine_jog_touch)
        goto action_reject;
    original_engine_jog_pulse = (engine_value_fn)install_hook(
        &engine_jog_pulse_hook, ENGINE_JOG_PULSE, engine_common_guard,
        (void *)hooked_engine_jog_pulse);
    if (!original_engine_jog_pulse)
        goto action_reject;
    original_engine_cue_standby = (engine_value_fn)install_hook(
        &engine_cue_standby_hook, ENGINE_CUE_STANDBY, engine_common_guard,
        (void *)hooked_engine_cue_standby);
    if (!original_engine_cue_standby)
        goto action_reject;
    original_engine_cue_scratch = (engine_value_fn)install_hook(
        &engine_cue_scratch_hook, ENGINE_CUE_SCRATCH, engine_common_guard,
        (void *)hooked_engine_cue_scratch);
    if (!original_engine_cue_scratch)
        goto action_reject;
    original_engine_loop_in = (engine_value_fn)install_hook(
        &engine_loop_in_hook, ENGINE_LOOP_IN, engine_common_guard,
        (void *)hooked_engine_loop_in);
    if (!original_engine_loop_in)
        goto action_reject;
    original_engine_loop_out = (engine_simple_fn)install_hook(
        &engine_loop_out_hook, ENGINE_LOOP_OUT, engine_simple_guard,
        (void *)hooked_engine_loop_out);
    if (!original_engine_loop_out)
        goto action_reject;
    original_engine_loop_exit = (engine_two_bool_fn)install_hook(
        &engine_loop_exit_hook, ENGINE_LOOP_EXIT, engine_wide_guard,
        (void *)hooked_engine_loop_exit);
    if (!original_engine_loop_exit)
        goto action_reject;
    original_engine_reloop = (engine_simple_fn)install_hook(
        &engine_reloop_hook, ENGINE_RELOOP, engine_simple_guard,
        (void *)hooked_engine_reloop);
    if (!original_engine_reloop)
        goto action_reject;
    original_engine_auto_loop = (engine_auto_loop_fn)install_hook(
        &engine_auto_loop_hook, ENGINE_AUTO_LOOP, engine_auto_loop_guard,
        (void *)hooked_engine_auto_loop);
    if (!original_engine_auto_loop)
        goto action_reject;
    original_engine_beat_jump = (engine_value_fn)install_hook(
        &engine_beat_jump_hook, ENGINE_BEAT_JUMP, engine_common_guard,
        (void *)hooked_engine_beat_jump);
    if (!original_engine_beat_jump)
        goto action_reject;
    original_engine_hot_cue_play = (engine_four_arg_fn)install_hook(
        &engine_hot_cue_play_hook, ENGINE_HOT_CUE_PLAY, engine_wide_guard,
        (void *)hooked_engine_hot_cue_play);
    if (!original_engine_hot_cue_play)
        goto action_reject;
    original_engine_hot_cue_record = (engine_value_fn)install_hook(
        &engine_hot_cue_record_hook, ENGINE_HOT_CUE_RECORD,
        engine_common_guard, (void *)hooked_engine_hot_cue_record);
    if (!original_engine_hot_cue_record)
        goto action_reject;
    original_engine_hot_cue_gate = (engine_value_fn)install_hook(
        &engine_hot_cue_gate_hook, ENGINE_HOT_CUE_GATE,
        engine_common_guard, (void *)hooked_engine_hot_cue_gate);
    if (!original_engine_hot_cue_gate)
        goto action_reject;
    original_network_status = (network_status_fn)install_hook(
        &network_status_hook, NETWORK_CHANGE_PLAY_STATUS,
        network_status_guard, (void *)hooked_network_status);
    if (!original_network_status)
        goto action_reject;
    original_key_manager_send_key = (key_manager_send_key_fn)install_hook(
        &key_manager_send_key_hook, KEY_MANAGER_SEND_KEY,
        key_manager_send_key_guard, (void *)hooked_key_manager_send_key);
    if (!original_key_manager_send_key)
        goto action_reject;

    pthread_t thread;
    if (pthread_create(&thread, 0, event_loop, 0)) {
        log_line("rejected: event tracer worker could not start");
        goto reject;
    }
    pthread_detach(thread);
    log_line("event tracer active: JSONL in /dev/shm/rx3-events.jsonl");
    return;

action_reject:
    log_line("rejected: direct action event prologue mismatch");

reject:
    uninstall_hook(&key_manager_send_key_hook);
    uninstall_hook(&network_status_hook);
    uninstall_hook(&engine_hot_cue_gate_hook);
    uninstall_hook(&engine_hot_cue_record_hook);
    uninstall_hook(&engine_hot_cue_play_hook);
    uninstall_hook(&engine_beat_jump_hook);
    uninstall_hook(&engine_auto_loop_hook);
    uninstall_hook(&engine_reloop_hook);
    uninstall_hook(&engine_loop_exit_hook);
    uninstall_hook(&engine_loop_out_hook);
    uninstall_hook(&engine_loop_in_hook);
    uninstall_hook(&engine_cue_scratch_hook);
    uninstall_hook(&engine_cue_standby_hook);
    uninstall_hook(&engine_jog_pulse_hook);
    uninstall_hook(&engine_jog_touch_hook);
    uninstall_hook(&engine_jog_speed_hook);
    uninstall_hook(&engine_tempo_hook);
    uninstall_hook(&engine_pause_hook);
    uninstall_hook(&engine_play_hook);
    uninstall_hook(&mixer_update_hook);
    uninstall_hook(&player_unload_hook);
    uninstall_hook(&player_load_hook);
    uninstall_hook(&player_status_hook);
    close(wake_socket[0]);
    close(wake_socket[1]);
    wake_socket[0] = -1;
    wake_socket[1] = -1;
}
