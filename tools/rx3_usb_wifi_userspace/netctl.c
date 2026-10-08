// SPDX-License-Identifier: MPL-2.0
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <limits.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

enum wait_mode {
    WAIT_IPV4,
    WAIT_ABSENT,
};

static int interface_has_lan_address(const char *interface)
{
    struct ifaddrs *addresses;
    struct ifaddrs *item;
    char text[INET_ADDRSTRLEN];
    unsigned int host_address;
    int found = 0;

    if (getifaddrs(&addresses) < 0)
        return 0;

    for (item = addresses; item; item = item->ifa_next) {
        struct sockaddr_in *address;

        if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
            strcmp(item->ifa_name, interface) != 0)
            continue;

        address = (struct sockaddr_in *)item->ifa_addr;
        host_address = ntohl(address->sin_addr.s_addr);
        if (host_address == 0 || (host_address >> 24) == 127 ||
            (host_address >> 16) == 0xa9fe)
            continue;
        if (!inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)))
            continue;

        puts(text);
        fflush(stdout);
        found = 1;
        break;
    }

    freeifaddrs(addresses);
    return found;
}

static int state_is_ready(enum wait_mode mode, const char *interface)
{
    unsigned int index = if_nametoindex(interface);

    switch (mode) {
    case WAIT_IPV4:
        return index != 0 && interface_has_lan_address(interface);
    case WAIT_ABSENT:
        return index == 0;
    }

    return 0;
}

static long long monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return -1;

    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int parse_timeout(const char *argument, int *timeout_seconds)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(argument, &end, 10);
    if (errno || *argument == '\0' || *end != '\0' || value < 0 ||
        value > INT_MAX / 1000)
        return 0;

    *timeout_seconds = (int)value;
    return 1;
}

static int rename_interface(const char *current_name, const char *new_name)
{
    struct ifreq request = {0};
    int socket_fd;

    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        perror("socket");
        return 1;
    }

    strcpy(request.ifr_name, current_name);
    strcpy(request.ifr_newname, new_name);
    if (ioctl(socket_fd, SIOCSIFNAME, &request) < 0) {
        perror("SIOCSIFNAME");
        close(socket_fd);
        return 1;
    }

    close(socket_fd);
    return 0;
}

static int open_route_socket(void)
{
    struct sockaddr_nl address = {
        .nl_family = AF_NETLINK,
        .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR,
    };
    int socket_fd;

    socket_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (socket_fd < 0)
        return -1;
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

static int wait_for_state(enum wait_mode mode, const char *interface,
                          int timeout_seconds)
{
    struct pollfd poll_fd;
    char messages[4096];
    long long deadline;
    long long now;
    int remaining;
    int socket_fd;

    if (timeout_seconds == 0)
        return state_is_ready(mode, interface) ? 0 : 1;

    socket_fd = open_route_socket();
    if (socket_fd < 0) {
        perror("route netlink");
        return 2;
    }

    if (state_is_ready(mode, interface)) {
        close(socket_fd);
        return 0;
    }

    now = monotonic_milliseconds();
    if (now < 0) {
        perror("clock_gettime");
        close(socket_fd);
        return 2;
    }
    deadline = now + (long long)timeout_seconds * 1000;
    poll_fd.fd = socket_fd;
    poll_fd.events = POLLIN;

    for (;;) {
        now = monotonic_milliseconds();
        if (now < 0 || now >= deadline)
            break;
        remaining = (int)(deadline - now);

        if (poll(&poll_fd, 1, remaining) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            close(socket_fd);
            return 2;
        }
        if (!(poll_fd.revents & POLLIN))
            break;
        if (recv(socket_fd, messages, sizeof(messages), 0) < 0) {
            if (errno == EINTR)
                continue;
            perror("route netlink receive");
            close(socket_fd);
            return 2;
        }
        if (state_is_ready(mode, interface)) {
            close(socket_fd);
            return 0;
        }
    }

    close(socket_fd);
    return 1;
}

int main(int argc, char **argv)
{
    enum wait_mode mode;
    int timeout_seconds;

    if (argc != 4) {
        fprintf(stderr,
                "usage: %s rename CURRENT NEW | ipv4|absent INTERFACE TIMEOUT\n",
                argv[0]);
        return 2;
    }
    if (strlen(argv[2]) >= IFNAMSIZ ||
        (strcmp(argv[1], "rename") == 0 && strlen(argv[3]) >= IFNAMSIZ)) {
        fprintf(stderr, "interface name is too long\n");
        return 2;
    }
    if (strcmp(argv[1], "rename") == 0)
        return rename_interface(argv[2], argv[3]);

    if (!parse_timeout(argv[3], &timeout_seconds)) {
        fprintf(stderr, "invalid timeout: %s\n", argv[3]);
        return 2;
    }

    if (strcmp(argv[1], "ipv4") == 0)
        mode = WAIT_IPV4;
    else if (strcmp(argv[1], "absent") == 0)
        mode = WAIT_ABSENT;
    else {
        fprintf(stderr, "unknown wait mode: %s\n", argv[1]);
        return 2;
    }

    return wait_for_state(mode, argv[2], timeout_seconds);
}
