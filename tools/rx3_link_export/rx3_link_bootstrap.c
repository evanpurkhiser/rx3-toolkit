/* SPDX-License-Identifier: MPL-2.0 */
/* Replays the stock USB-mounted callback when the Linux host omits it. */

typedef unsigned int size_t;
typedef int ssize_t;
typedef unsigned int uint32_t;
typedef unsigned long pthread_t;

struct timespec {
    long seconds;
    long nanoseconds;
};

extern int open(const char *, int, ...);
extern ssize_t write(int, const void *, size_t);
extern ssize_t readlink(const char *, char *, size_t);
extern int close(int);
extern int memcmp(const void *, const void *, size_t);
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_detach(pthread_t);
extern int nanosleep(const struct timespec *, struct timespec *);

#define O_WRONLY 1
#define O_CREAT 0100
#define O_APPEND 02000
#define GET_PC_CONTROLLER 0x0031df64u
#define HANDLE_USB_MOUNT_MESSAGE 0x002e9700u
#define NETWORK_MANAGER_SINGLETON 0x026873d8u
#define SYSTEM_MANAGER_CHANGE_STATE 0x00393438u
#define PC_CONTROLLER_VTABLE 0x004d0198u
#define PC_CONTROL_KEY_SERVER_VTABLE 0x004dd758u
#define PC_CONTROL_VIEW_SERVER_VTABLE 0x004d0648u
#define NETWORK_MANAGER_VTABLE 0x004e0fa0u
#define PRO_DJ_LINK_VTABLE 0x004e1340u
#define SYSTEM_MANAGER_VTABLE 0x004e1490u
#define SYSTEM_STATE_DISCOVERY 1
#define SYSTEM_STATE_CONNECTING 5
#define SYSTEM_STATE_LINK_STOP 6

typedef void *(*get_pc_controller_fn)(void);
typedef void (*handle_usb_mount_fn)(void *, int, const void *, int,
                                    int, int, int, int);
typedef void (*change_state_fn)(void *, int);

static void log_line(const char *message)
{
    int fd = open("/tmp/rx3-link-bootstrap.log",
                  O_WRONLY | O_CREAT | O_APPEND, 0600);
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

static void pause_seconds(long seconds)
{
    struct timespec delay = {seconds, 0};
    (void)nanosleep(&delay, 0);
}

static int plausible_pointer(uint32_t pointer)
{
    return pointer >= 0x01000000u && pointer < 0x40000000u &&
           !(pointer & 3u);
}

static int firmware_guards_match(void)
{
    static const unsigned char get_pc_controller[8] = {
        0xb0, 0x36, 0x06, 0xe3, 0x68, 0x32, 0x40, 0xe3,
    };
    static const unsigned char handle_mount[8] = {
        0x03, 0x00, 0x51, 0xe3, 0x10, 0x40, 0x2d, 0xe9,
    };
    static const unsigned char change_state[8] = {
        0xf0, 0x40, 0x2d, 0xe9, 0x00, 0x40, 0xa0, 0xe1,
    };

    return !memcmp((const void *)GET_PC_CONTROLLER,
                   get_pc_controller, sizeof(get_pc_controller)) &&
           !memcmp((const void *)HANDLE_USB_MOUNT_MESSAGE,
                   handle_mount, sizeof(handle_mount)) &&
           !memcmp((const void *)SYSTEM_MANAGER_CHANGE_STATE,
                   change_state, sizeof(change_state));
}

static int change_link_stop_state(int next_state)
{
    volatile uint32_t *network_manager;
    volatile uint32_t *pro_dj_link;
    volatile uint32_t *system_manager;
    volatile unsigned char *system_bytes;

    network_manager = *(volatile uint32_t **)NETWORK_MANAGER_SINGLETON;
    if (!plausible_pointer((uint32_t)network_manager) ||
        network_manager[0] != NETWORK_MANAGER_VTABLE)
        return 0;
    pro_dj_link = (volatile uint32_t *)network_manager[0xbcu / 4u];
    if (!plausible_pointer((uint32_t)pro_dj_link) ||
        pro_dj_link[0] != PRO_DJ_LINK_VTABLE)
        return 0;
    system_manager = (volatile uint32_t *)pro_dj_link[0x10u / 4u];
    if (!plausible_pointer((uint32_t)system_manager) ||
        system_manager[0] != SYSTEM_MANAGER_VTABLE)
        return 0;

    system_bytes = (volatile unsigned char *)system_manager;
    if (system_bytes[0x3cu] != SYSTEM_STATE_LINK_STOP)
        return 0;
    ((change_state_fn)SYSTEM_MANAGER_CHANGE_STATE)(
        (void *)system_manager, next_state
    );
    return 1;
}

static void *bootstrap(void *unused)
{
    void *pc_controller;
    volatile uint32_t *pc;
    volatile uint32_t *key_server;
    volatile uint32_t *view_server;
    int attempt;

    (void)unused;
    pause_seconds(15);

    if (!firmware_guards_match()) {
        log_line("link bootstrap: firmware guard rejected\n");
        return 0;
    }

    pc_controller = 0;
    for (attempt = 0; attempt < 30; attempt++) {
        pc_controller = ((get_pc_controller_fn)GET_PC_CONTROLLER)();
        if (plausible_pointer((uint32_t)pc_controller)) {
            pc = (volatile uint32_t *)pc_controller;
            key_server = (volatile uint32_t *)pc[0x58u / 4u];
            view_server = (volatile uint32_t *)pc[0x5cu / 4u];
            if (pc[0] == PC_CONTROLLER_VTABLE &&
                plausible_pointer((uint32_t)key_server) &&
                key_server[0] == PC_CONTROL_KEY_SERVER_VTABLE &&
                plausible_pointer((uint32_t)view_server) &&
                view_server[0] == PC_CONTROL_VIEW_SERVER_VTABLE)
                break;
        }
        pause_seconds(1);
    }
    if (attempt == 30) {
        log_line("link bootstrap: PcController graph unavailable\n");
        return 0;
    }

    ((handle_usb_mount_fn)HANDLE_USB_MOUNT_MESSAGE)(
        pc_controller, 3, 0, 0, 0, 0, 0, 0
    );
    __sync_synchronize();
    if (*((volatile unsigned char *)pc_controller + 0x72u) != 1) {
        log_line("link bootstrap: stock callback did not set mounted flag\n");
        return 0;
    }
    log_line("link bootstrap: stock USB-mounted callback invoked\n");
    pause_seconds(3);
    if (change_link_stop_state(SYSTEM_STATE_DISCOVERY))
        log_line("link bootstrap: LinkStop resumed at Discovery\n");
    pause_seconds(5);
    if (change_link_stop_state(SYSTEM_STATE_CONNECTING))
        log_line("link bootstrap: LinkStop resumed at Connecting\n");
    return 0;
}

__attribute__((constructor)) static void initialize(void)
{
    pthread_t worker;

    if (!running_in_rbp())
        return;
    if (pthread_create(&worker, 0, bootstrap, 0)) {
        log_line("link bootstrap: worker creation failed\n");
        return;
    }
    (void)pthread_detach(worker);
}
