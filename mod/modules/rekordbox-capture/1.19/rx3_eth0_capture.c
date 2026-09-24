// SPDX-License-Identifier: MPL-2.0
/* Freestanding, receive-only eth0 packet recorder for Linux/ARM EABI. */

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

struct timeval { long tv_sec; long tv_usec; };
struct ifreq { char name[16]; int index; uint8_t padding[20]; };
struct sockaddr_ll {
    uint16_t family;
    uint16_t protocol;
    int index;
    uint16_t hatype;
    uint8_t packet_type;
    uint8_t address_length;
    uint8_t address[8];
};

#define SYS_EXIT 1
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_IOCTL 54
#define SYS_GETTIMEOFDAY 78
#define SYS_SOCKET 281
#define SYS_BIND 282
#define SYS_RECVFROM 292

#define O_WRONLY 1
#define O_CREAT 0100
#define O_TRUNC 01000
#define AF_PACKET 17
#define SOCK_RAW 3
#define ETH_P_ALL 0x0003
#define SIOCGIFINDEX 0x8933
#define PCAP_LIMIT (128u * 1024u * 1024u)
#define SNAPLEN 65535u

static long syscall1(long number, long a0)
{
    register long r0 __asm__("r0") = a0;
    register long r7 __asm__("r7") = number;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}

static long syscall2(long number, long a0, long a1)
{
    register long r0 __asm__("r0") = a0;
    register long r1 __asm__("r1") = a1;
    register long r7 __asm__("r7") = number;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
    return r0;
}

static long syscall3(long number, long a0, long a1, long a2)
{
    register long r0 __asm__("r0") = a0;
    register long r1 __asm__("r1") = a1;
    register long r2 __asm__("r2") = a2;
    register long r7 __asm__("r7") = number;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}

static long syscall6(long number, long a0, long a1, long a2,
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
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7)
                     : "memory");
    return r0;
}

static uint16_t network_short(uint16_t value)
{
    return (uint16_t)((value << 8u) | (value >> 8u));
}

static void copy_string(char *target, const char *source, uint32_t limit)
{
    uint32_t index = 0;
    while (index + 1u < limit && source[index]) {
        target[index] = source[index];
        index++;
    }
    target[index] = 0;
}

static uint32_t string_length(const char *text)
{
    uint32_t length = 0;
    while (text[length]) length++;
    return length;
}

static int write_all(int fd, const void *buffer, uint32_t length)
{
    const uint8_t *bytes = buffer;
    uint32_t written = 0;
    while (written < length) {
        long count = syscall3(SYS_WRITE, fd, (long)(bytes + written), length - written);
        if (count <= 0) return 0;
        written += (uint32_t)count;
    }
    return 1;
}

static void log_message(const char *path, const char *message)
{
    int fd = (int)syscall3(SYS_OPEN, (long)path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    (void)write_all(fd, message, string_length(message));
    (void)write_all(fd, "\n", 1u);
    (void)syscall1(SYS_CLOSE, fd);
}

static int publish_ready(const char *path, const char *capture_path)
{
    int fd = (int)syscall3(SYS_OPEN, (long)path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return 0;
    int okay = write_all(fd, capture_path, string_length(capture_path)) &&
               write_all(fd, "\n", 1u);
    (void)syscall1(SYS_CLOSE, fd);
    return okay;
}

struct pcap_header {
    uint32_t magic;
    uint16_t major;
    uint16_t minor;
    int timezone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
};

struct pcap_packet {
    uint32_t seconds;
    uint32_t microseconds;
    uint32_t included;
    uint32_t original;
};

int entry(unsigned long *stack);

static int capture(const char *interface, const char *output,
                   const char *ready, const char *log)
{
    int socket_fd = (int)syscall3(SYS_SOCKET, AF_PACKET, SOCK_RAW,
                                  network_short(ETH_P_ALL));
    if (socket_fd < 0) {
        log_message(log, "rejected: AF_PACKET socket failed");
        return 1;
    }

    struct ifreq request = {{0}, 0, {0}};
    copy_string(request.name, interface, sizeof(request.name));
    if (syscall3(SYS_IOCTL, socket_fd, SIOCGIFINDEX, (long)&request) < 0) {
        log_message(log, "rejected: eth0 interface lookup failed");
        return 1;
    }
    struct sockaddr_ll address = {0};
    address.family = AF_PACKET;
    address.protocol = network_short(ETH_P_ALL);
    address.index = request.index;
    if (syscall3(SYS_BIND, socket_fd, (long)&address, sizeof(address)) < 0) {
        log_message(log, "rejected: eth0 packet socket bind failed");
        return 1;
    }

    int output_fd = (int)syscall3(SYS_OPEN, (long)output,
                                  O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (output_fd < 0) {
        log_message(log, "rejected: PCAP output could not be created");
        return 1;
    }
    const struct pcap_header header = {
        0xa1b2c3d4u, 2u, 4u, 0, 0u, SNAPLEN, 1u
    };
    if (!write_all(output_fd, &header, sizeof(header)) ||
        !publish_ready(ready, output)) {
        log_message(log, "rejected: PCAP header or readiness write failed");
        return 1;
    }

    uint32_t size = sizeof(header);
    uint8_t packet[SNAPLEN];
    while (size + sizeof(struct pcap_packet) <= PCAP_LIMIT) {
        long count = syscall6(SYS_RECVFROM, socket_fd, (long)packet, sizeof(packet),
                              0, 0, 0);
        if (count <= 0) continue;
        if (size + sizeof(struct pcap_packet) + (uint32_t)count > PCAP_LIMIT) break;

        struct timeval now = {0, 0};
        (void)syscall2(SYS_GETTIMEOFDAY, (long)&now, 0);
        struct pcap_packet record = {
            (uint32_t)now.tv_sec, (uint32_t)now.tv_usec,
            (uint32_t)count, (uint32_t)count
        };
        if (!write_all(output_fd, &record, sizeof(record)) ||
            !write_all(output_fd, packet, (uint32_t)count)) {
            log_message(log, "capture stopped: PCAP output write failed");
            return 1;
        }
        size += sizeof(record) + (uint32_t)count;
    }

    log_message(log, "capture stopped: 128 MiB PCAP limit reached");
    (void)syscall1(SYS_CLOSE, output_fd);
    (void)syscall1(SYS_CLOSE, socket_fd);
    return 0;
}

__attribute__((naked, noreturn)) void _start(void)
{
    __asm__ volatile(
        "mov r0, sp\n"
        "bl entry\n"
        "mov r7, #1\n"
        "svc 0\n"
    );
}

int entry(unsigned long *stack)
{
    int argc = (int)stack[0];
    char **argv = (char **)&stack[1];
    if (argc != 5) return 2;
    return capture(argv[1], argv[2], argv[3], argv[4]);
}
