/* SPDX-License-Identifier: MPL-2.0 */
/* Establishes the stock mounted state for Link Export. */
#include "../core/api/rx3_module_api.h"

#define GET_PC_CONTROLLER 0x0031df64u
#define HANDLE_USB_MOUNT_MESSAGE 0x002e9700u
#define GET_PC_CONTROL_CERT_INSTANCE 0x00367ca0u
#define CHECK_CERT_STATUS 0x00367e1cu
#define PC_CONTROL_CERT_SINGLETON 0x026870c4u
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
typedef void *(*get_pc_control_cert_fn)(void);
typedef uint8_t (*check_cert_status_fn)(void *);
typedef void (*change_state_fn)(void *, int);

static const struct rx3_services *framework;
static pthread_t worker_thread;
static unsigned int worker_started;
static unsigned int worker_running;

static int pause_seconds(unsigned int seconds)
{
    for (unsigned int tick = 0; tick < seconds * 10u; tick++) {
        if (!__atomic_load_n(&worker_running, __ATOMIC_SEQ_CST))
            return 0;
        usleep(100000u);
    }
    return __atomic_load_n(&worker_running, __ATOMIC_SEQ_CST) != 0;
}

static int plausible_pointer(unsigned long pointer)
{
    return pointer >= 0x01000000ul && pointer < 0x40000000ul &&
           !(pointer & 3ul);
}

static int firmware_guards_match(void)
{
    static const uint8_t get_pc_controller[8] = {
        0xb0, 0x36, 0x06, 0xe3, 0x68, 0x32, 0x40, 0xe3,
    };
    static const uint8_t handle_mount[8] = {
        0x03, 0x00, 0x51, 0xe3, 0x10, 0x40, 0x2d, 0xe9,
    };
    static const uint8_t change_state[8] = {
        0xf0, 0x40, 0x2d, 0xe9, 0x00, 0x40, 0xa0, 0xe1,
    };
    static const uint8_t get_cert[8] = {
        0x54, 0x00, 0x9f, 0xe5, 0x08, 0x40, 0x2d, 0xe9,
    };
    static const uint8_t check_cert[8] = {
        0x00, 0x30, 0x90, 0xe5, 0x10, 0x40, 0x2d, 0xe9,
    };

    return !memcmp((const void *)(unsigned long)GET_PC_CONTROLLER,
                   get_pc_controller, sizeof(get_pc_controller)) &&
           !memcmp((const void *)(unsigned long)HANDLE_USB_MOUNT_MESSAGE,
                   handle_mount, sizeof(handle_mount)) &&
           !memcmp((const void *)(unsigned long)GET_PC_CONTROL_CERT_INSTANCE,
                   get_cert, sizeof(get_cert)) &&
           !memcmp((const void *)(unsigned long)CHECK_CERT_STATUS,
                   check_cert, sizeof(check_cert)) &&
           !memcmp((const void *)(unsigned long)SYSTEM_MANAGER_CHANGE_STATE,
                   change_state, sizeof(change_state));
}

static int certify_pc_control(void)
{
    void *cert = ((get_pc_control_cert_fn)(unsigned long)
                  GET_PC_CONTROL_CERT_INSTANCE)();
    if ((unsigned long)cert != PC_CONTROL_CERT_SINGLETON)
        return 0;

    (void)((check_cert_status_fn)(unsigned long)CHECK_CERT_STATUS)(cert);
    __sync_synchronize();
    return *((volatile uint8_t *)cert + 0x0cu) == 1u;
}

static void *find_pc_controller(void)
{
    for (unsigned int attempt = 0; attempt < 30u; attempt++) {
        void *controller = ((get_pc_controller_fn)(unsigned long)
                            GET_PC_CONTROLLER)();
        if (plausible_pointer((unsigned long)controller)) {
            volatile uint32_t *pc = controller;
            volatile uint32_t *key_server =
                (volatile uint32_t *)(unsigned long)pc[0x58u / 4u];
            volatile uint32_t *view_server =
                (volatile uint32_t *)(unsigned long)pc[0x5cu / 4u];
            if (pc[0] == PC_CONTROLLER_VTABLE &&
                plausible_pointer((unsigned long)key_server) &&
                key_server[0] == PC_CONTROL_KEY_SERVER_VTABLE &&
                plausible_pointer((unsigned long)view_server) &&
                view_server[0] == PC_CONTROL_VIEW_SERVER_VTABLE)
                return controller;
        }
        if (!pause_seconds(1u))
            return 0;
    }
    return 0;
}

static int change_link_stop_state(int next_state)
{
    volatile uint32_t *network_manager =
        *(volatile uint32_t **)(unsigned long)NETWORK_MANAGER_SINGLETON;
    if (!plausible_pointer((unsigned long)network_manager) ||
        network_manager[0] != NETWORK_MANAGER_VTABLE)
        return 0;

    volatile uint32_t *pro_dj_link =
        (volatile uint32_t *)(unsigned long)network_manager[0xbcu / 4u];
    if (!plausible_pointer((unsigned long)pro_dj_link) ||
        pro_dj_link[0] != PRO_DJ_LINK_VTABLE)
        return 0;

    volatile uint32_t *system_manager =
        (volatile uint32_t *)(unsigned long)pro_dj_link[0x10u / 4u];
    if (!plausible_pointer((unsigned long)system_manager) ||
        system_manager[0] != SYSTEM_MANAGER_VTABLE)
        return 0;

    volatile uint8_t *system_bytes = (volatile uint8_t *)system_manager;
    if (system_bytes[0x3cu] != SYSTEM_STATE_LINK_STOP)
        return 0;

    ((change_state_fn)(unsigned long)SYSTEM_MANAGER_CHANGE_STATE)(
        (void *)system_manager, next_state);
    return 1;
}

struct link_export_operations {
    int (*guards_match)(void);
    void *(*find_controller)(void);
    void (*mount)(void *);
    int (*certify)(void);
    int (*change_state)(int);
    int (*pause)(unsigned int);
};

static void mount_pc_controller(void *controller)
{
    ((handle_usb_mount_fn)(unsigned long)HANDLE_USB_MOUNT_MESSAGE)(
        controller, 3, 0, 0, 0, 0, 0, 0);
}

static struct link_export_operations operations = {
    firmware_guards_match, find_pc_controller, mount_pc_controller,
    certify_pc_control, change_link_stop_state, pause_seconds,
};

static void *activate_link_export(void *unused)
{
    (void)unused;
    if (!operations.pause(15u))
        return 0;
    if (!operations.guards_match()) {
        framework->log_line("link export activate: firmware guard rejected");
        return 0;
    }

    void *controller = operations.find_controller();
    if (!controller) {
        framework->log_line("link export activate: PcController graph unavailable");
        return 0;
    }

    operations.mount(controller);
    __sync_synchronize();
    if (*((volatile uint8_t *)controller + 0x72u) != 1u) {
        framework->log_line(
            "link export activate: stock callback did not set mounted flag");
        return 0;
    }
    framework->log_line("link export activate: stock USB-mounted callback invoked");
    if (operations.certify())
        framework->log_line("link export activate: stock PC certification asserted");
    else
        framework->log_line("link export activate: stock PC certification failed");

    if (!operations.pause(3u))
        return 0;
    if (operations.change_state(SYSTEM_STATE_DISCOVERY))
        framework->log_line("link export activate: LinkStop resumed at Discovery");

    if (!operations.pause(5u))
        return 0;
    if (operations.change_state(SYSTEM_STATE_CONNECTING))
        framework->log_line("link export activate: LinkStop resumed at Connecting");
    return 0;
}

static int configured(void)
{
    const char *setting = getenv("RX3_LINK_EXPORT_ACTIVATE");
    return setting && setting[0] == '1';
}

static int start(const struct rx3_services *services)
{
    framework = services;
    __atomic_store_n(&worker_running, 1u, __ATOMIC_SEQ_CST);
    if (pthread_create(&worker_thread, 0, activate_link_export, 0)) {
        __atomic_store_n(&worker_running, 0u, __ATOMIC_SEQ_CST);
        framework->log_line("link export activate: worker creation failed");
        return 0;
    }
    worker_started = 1u;
    framework->log_line("link export activate: one-shot worker armed");
    return 1;
}

static void stop(void)
{
    __atomic_store_n(&worker_running, 0u, __ATOMIC_SEQ_CST);
    if (worker_started) {
        pthread_join(worker_thread, 0);
        worker_started = 0u;
    }
    framework = 0;
}

const struct rx3_module rx3_link_export_activate_module = {
    RX3_MODULE_API_VERSION, sizeof(struct rx3_module), "link-export-activate",
    configured, start, stop, 0, 0, 0, 0, 0,
};
