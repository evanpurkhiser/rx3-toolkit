/* SPDX-License-Identifier: MPL-2.0
 * Cached RX3 client for the S3 private Ethernet control plane.
 */

#ifndef RX3_S3_CONFIG_CLIENT_H
#define RX3_S3_CONFIG_CLIENT_H

#define RX3_S3_CONFIG_FREESTANDING 1
#include "../../../../firmware/esp32-s3-link/include/rx3_s3_config_protocol.h"

#define RX3_S3_INTERFACE "usb0"
#define RX3_S3_FRAME_CAPACITY 128u
#define RX3_S3_REFRESH_US 1000000u
#define RX3_S3_AF_PACKET 17
#define RX3_S3_SOCK_RAW 3
#define RX3_S3_SIOCGIFINDEX 0x8933
#define RX3_S3_SIOCGIFHWADDR 0x8927
#define RX3_S3_SIOCGIFADDR 0x8915
#define RX3_S3_SYS_IOCTL 54
#define RX3_S3_SYS_SOCKET 281
#define RX3_S3_SYS_BIND 282
#define RX3_S3_SYS_SENDTO 290
#define RX3_S3_SYS_RECVFROM 292

struct rx3_s3_sockaddr_ll {
    uint16_t family;
    uint16_t protocol;
    int index;
    uint16_t hatype;
    uint8_t packet_type;
    uint8_t address_length;
    uint8_t address[8];
};

struct rx3_s3_ifreq {
    char name[16];
    uint8_t value[24];
};

struct rx3_s3_pollfd { int fd; short events; short revents; };

struct rx3_s3_snapshot {
    uint8_t reachable;
    uint8_t wifi_connected;
    uint8_t password_set;
    signed char rssi;
    uint8_t mac[6];
    uint8_t ipv4[4];
    char ssid[RX3_S3_CONFIG_SSID_MAX_LENGTH + 1u];
};


extern int poll(struct rx3_s3_pollfd *, unsigned long, int);

static volatile int rx3_s3_cache_lock;
static struct rx3_s3_snapshot rx3_s3_cache;
static int rx3_s3_worker_state;
static volatile uint16_t rx3_s3_request_id;

static long rx3_s3_syscall3(long number, long a0, long a1, long a2)
{
    register long r0 __asm__("r0") = a0;
    register long r1 __asm__("r1") = a1;
    register long r2 __asm__("r2") = a2;
    register long r7 __asm__("r7") = number;
    __asm__ volatile("svc 0" : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}

static long rx3_s3_syscall6(long number, long a0, long a1, long a2,
                            long a3, long a4, long a5)
{
    register long r0 __asm__("r0") = a0;
    register long r1 __asm__("r1") = a1;
    register long r2 __asm__("r2") = a2;
    register long r3 __asm__("r3") = a3;
    register long r4 __asm__("r4") = a4;
    register long r5 __asm__("r5") = a5;
    register long r7 __asm__("r7") = number;
    __asm__ volatile("svc 0" : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5),
                       "r"(r7) : "memory");
    return r0;
}

static uint16_t rx3_s3_network_short(uint16_t value)
{
    return (uint16_t)((value << 8u) | (value >> 8u));
}

static unsigned int rx3_s3_string_length(const char *text,
                                         unsigned int capacity)
{
    unsigned int length = 0;
    while (length < capacity && text[length])
        length++;
    return length;
}

static void rx3_s3_copy_text(char *destination, unsigned int capacity,
                             const char *source, unsigned int length)
{
    if (!capacity)
        return;
    if (length >= capacity)
        length = capacity - 1u;
    memcpy(destination, source, length);
    destination[length] = 0;
}

static void rx3_s3_publish(const struct rx3_s3_snapshot *snapshot)
{
    while (__sync_lock_test_and_set(&rx3_s3_cache_lock, 1))
        ;
    rx3_s3_cache = *snapshot;
    __sync_lock_release(&rx3_s3_cache_lock);
}

static void rx3_s3_read_snapshot(struct rx3_s3_snapshot *snapshot)
{
    while (__sync_lock_test_and_set(&rx3_s3_cache_lock, 1))
        ;
    *snapshot = rx3_s3_cache;
    __sync_lock_release(&rx3_s3_cache_lock);
}

static unsigned int rx3_s3_append_unsigned(char *output, unsigned int offset,
                                            unsigned int capacity,
                                            unsigned int value)
{
    char reverse[10];
    unsigned int count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof(reverse));
    while (count && offset + 1u < capacity)
        output[offset++] = reverse[--count];
    output[offset] = 0;
    return offset;
}

static void rx3_s3_format_ipv4(char *output, unsigned int capacity,
                               const uint8_t address[4])
{
    unsigned int offset = 0;
    if (!address[0] && !address[1] && !address[2] && !address[3]) {
        rx3_s3_copy_text(output, capacity, "UNASSIGNED", 10u);
        return;
    }
    for (unsigned int i = 0; i < 4u; i++) {
        offset = rx3_s3_append_unsigned(output, offset, capacity, address[i]);
        if (i != 3u && offset + 1u < capacity) {
            output[offset++] = '.';
            output[offset] = 0;
        }
    }
}

static char rx3_s3_hex_digit(unsigned int value)
{
    return (char)(value < 10u ? '0' + value : 'A' + value - 10u);
}

static void rx3_s3_format_mac(char *output, unsigned int capacity,
                              const uint8_t mac[6])
{
    if (capacity < 18u) {
        if (capacity)
            output[0] = 0;
        return;
    }
    unsigned int offset = 0;
    for (unsigned int i = 0; i < 6u; i++) {
        output[offset++] = rx3_s3_hex_digit(mac[i] >> 4u);
        output[offset++] = rx3_s3_hex_digit(mac[i] & 0x0fu);
        if (i != 5u)
            output[offset++] = ':';
    }
    output[offset] = 0;
}

static int rx3_s3_parse_status(const uint8_t *payload, unsigned int length,
                               struct rx3_s3_snapshot *snapshot)
{
    if (length < RX3_S3_CONFIG_STATUS_FIXED_SIZE)
        return 0;
    const unsigned int ssid_length = payload[8];
    if (ssid_length > RX3_S3_CONFIG_SSID_MAX_LENGTH ||
        length != RX3_S3_CONFIG_STATUS_FIXED_SIZE + ssid_length)
        return 0;
    const uint8_t flags = payload[0];
    snapshot->reachable = 1u;
    snapshot->wifi_connected = !!(flags & RX3_S3_CONFIG_FLAG_WIFI_CONNECTED);
    snapshot->password_set = !!(flags & RX3_S3_CONFIG_FLAG_PASSWORD_SET);
    snapshot->rssi = (signed char)payload[1];
    memcpy(snapshot->mac, payload + 2u, sizeof(snapshot->mac));
    rx3_s3_copy_text(snapshot->ssid, sizeof(snapshot->ssid),
                     (const char *)payload + RX3_S3_CONFIG_STATUS_FIXED_SIZE,
                     ssid_length);
    return 1;
}


static unsigned int rx3_s3_build_payload(uint8_t *payload, uint8_t opcode,
                                          uint16_t request_id,
                                          const char *ssid,
                                          unsigned int ssid_length,
                                          const char *password,
                                          unsigned int password_length)
{
    unsigned int body_length = opcode == RX3_S3_CONFIG_SET_CREDENTIALS
                                   ? 2u + ssid_length + password_length : 0u;
    memcpy(payload, "RX3C", 4u);
    payload[4] = opcode;
    payload[5] = RX3_S3_CONFIG_OK;
    payload[6] = (uint8_t)(request_id >> 8u);
    payload[7] = (uint8_t)request_id;
    payload[8] = (uint8_t)(body_length >> 8u);
    payload[9] = (uint8_t)body_length;
    if (body_length) {
        payload[10] = (uint8_t)ssid_length;
        payload[11] = (uint8_t)password_length;
        memcpy(payload + 12u, ssid, ssid_length);
        memcpy(payload + 12u + ssid_length, password, password_length);
    }
    return RX3_S3_CONFIG_HEADER_SIZE + body_length;
}

static int rx3_s3_parse_response(const uint8_t *payload, unsigned int length,
                                 uint8_t opcode, uint16_t request_id,
                                 const uint8_t **body,
                                 unsigned int *body_length)
{
    if (length < RX3_S3_CONFIG_HEADER_SIZE ||
        memcmp(payload, "RX3C", 4u) ||
        payload[4] != (uint8_t)(opcode | RX3_S3_CONFIG_RESPONSE_BIT) ||
        payload[5] != RX3_S3_CONFIG_OK ||
        payload[6] != (uint8_t)(request_id >> 8u) ||
        payload[7] != (uint8_t)request_id)
        return 0;
    const unsigned int declared = ((unsigned int)payload[8] << 8u) | payload[9];
    if (declared > length - RX3_S3_CONFIG_HEADER_SIZE)
        return 0;
    *body = payload + RX3_S3_CONFIG_HEADER_SIZE;
    *body_length = declared;
    return 1;
}

static int rx3_s3_interface_info(int descriptor, int *interface_index,
                                 uint8_t mac[6], uint8_t ipv4[4])
{
    struct rx3_s3_ifreq request;
    memset(&request, 0, sizeof(request));
    rx3_s3_copy_text(request.name, sizeof(request.name), RX3_S3_INTERFACE, 4u);
    if (rx3_s3_syscall3(RX3_S3_SYS_IOCTL, descriptor,
                        RX3_S3_SIOCGIFINDEX, (long)&request) < 0)
        return 0;
    memcpy(interface_index, request.value, sizeof(*interface_index));
    if (rx3_s3_syscall3(RX3_S3_SYS_IOCTL, descriptor,
                        RX3_S3_SIOCGIFHWADDR, (long)&request) < 0)
        return 0;
    memcpy(mac, request.value + 2u, 6u);
    memset(ipv4, 0, 4u);
    if (rx3_s3_syscall3(RX3_S3_SYS_IOCTL, descriptor,
                        RX3_S3_SIOCGIFADDR, (long)&request) >= 0)
        memcpy(ipv4, request.value + 4u, 4u);
    return 1;
}

static int rx3_s3_exchange(int descriptor,
                           const struct rx3_s3_sockaddr_ll *address,
                           const uint8_t source_mac[6], uint8_t opcode,
                           const char *ssid, unsigned int ssid_length,
                           const char *password, unsigned int password_length,
                           struct rx3_s3_snapshot *snapshot)
{
    uint8_t frame[RX3_S3_FRAME_CAPACITY];
    memset(frame, 0, sizeof(frame));
    memset(frame, 0xff, 6u);
    memcpy(frame + 6u, source_mac, 6u);
    frame[12] = (uint8_t)(RX3_S3_CONFIG_ETHERTYPE >> 8u);
    frame[13] = (uint8_t)RX3_S3_CONFIG_ETHERTYPE;
    const uint16_t request_id = ++rx3_s3_request_id;
    const unsigned int payload_length = rx3_s3_build_payload(
        frame + 14u, opcode, request_id, ssid, ssid_length,
        password, password_length);
    const unsigned int frame_length = 14u + payload_length < 60u
                                          ? 60u : 14u + payload_length;
    if (rx3_s3_syscall6(RX3_S3_SYS_SENDTO, descriptor, (long)frame,
                        frame_length, 0, (long)address, sizeof(*address)) < 0)
        return 0;

    struct rx3_s3_pollfd wait = {descriptor, 1, 0};
    while (poll(&wait, 1u, 500) > 0) {
        const long received = rx3_s3_syscall6(
            RX3_S3_SYS_RECVFROM, descriptor, (long)frame, sizeof(frame),
            0, 0, 0);
        if (received < 14 + RX3_S3_CONFIG_HEADER_SIZE ||
            frame[12] != (RX3_S3_CONFIG_ETHERTYPE >> 8u) ||
            frame[13] != (RX3_S3_CONFIG_ETHERTYPE & 0xffu))
            continue;
        const uint8_t *body;
        unsigned int body_length;
        if (!rx3_s3_parse_response(frame + 14u,
                                   (unsigned int)received - 14u,
                                   opcode, request_id, &body, &body_length))
            continue;
        return opcode != RX3_S3_CONFIG_GET_STATUS ||
               rx3_s3_parse_status(body, body_length, snapshot);
    }
    return 0;
}


static void *rx3_s3_worker(void *unused)
{
    (void)unused;
    while (__sync_val_compare_and_swap(&rx3_s3_worker_state, 1, 1) == 1) {
        struct rx3_s3_snapshot next;
        memset(&next, 0, sizeof(next));
        next.rssi = -127;
        const int descriptor = (int)rx3_s3_syscall3(
            RX3_S3_SYS_SOCKET, RX3_S3_AF_PACKET, RX3_S3_SOCK_RAW,
            rx3_s3_network_short(RX3_S3_CONFIG_ETHERTYPE));
        int interface_index;
        uint8_t source_mac[6];
        if (descriptor >= 0 && rx3_s3_interface_info(
                descriptor, &interface_index, source_mac, next.ipv4)) {
            struct rx3_s3_sockaddr_ll address;
            memset(&address, 0, sizeof(address));
            address.family = RX3_S3_AF_PACKET;
            address.protocol = rx3_s3_network_short(RX3_S3_CONFIG_ETHERTYPE);
            address.index = interface_index;
            address.address_length = 6u;
            memset(address.address, 0xff, 6u);
            if (rx3_s3_syscall3(RX3_S3_SYS_BIND, descriptor,
                                (long)&address, sizeof(address)) >= 0) {
                (void)rx3_s3_exchange(
                    descriptor, &address, source_mac,
                    RX3_S3_CONFIG_GET_STATUS, 0, 0u, 0, 0u, &next);
            }
        }
        if (descriptor >= 0)
            close(descriptor);
        rx3_s3_publish(&next);
        usleep(RX3_S3_REFRESH_US);
    }
    __sync_lock_test_and_set(&rx3_s3_worker_state, 2);
    return 0;
}

static int rx3_s3_client_start(void)
{
    if (__sync_val_compare_and_swap(&rx3_s3_worker_state, 0, 1) != 0)
        return 0;
    pthread_t thread;
    if (pthread_create(&thread, 0, rx3_s3_worker, 0)) {
        __sync_lock_test_and_set(&rx3_s3_worker_state, 0);
        return 0;
    }
    pthread_detach(thread);
    return 1;
}

static void rx3_s3_client_stop(void)
{
    const int previous = __sync_val_compare_and_swap(
        &rx3_s3_worker_state, 1, 0);
    if (previous == 0)
        return;
    if (previous == 2) {
        __sync_lock_test_and_set(&rx3_s3_worker_state, 0);
        return;
    }
    while (__sync_val_compare_and_swap(&rx3_s3_worker_state, 2, 2) != 2)
        usleep(10000u);
    __sync_lock_test_and_set(&rx3_s3_worker_state, 0);
}

#endif /* RX3_S3_CONFIG_CLIENT_H */
