// SPDX-License-Identifier: MPL-2.0
#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct ifreq request = {0};
    int socket_fd;

    if (argc != 3) {
        fprintf(stderr, "usage: %s CURRENT_NAME NEW_NAME\n", argv[0]);
        return 2;
    }
    if (strlen(argv[1]) >= IFNAMSIZ || strlen(argv[2]) >= IFNAMSIZ) {
        fprintf(stderr, "interface name is too long\n");
        return 2;
    }

    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
        perror("socket");
        return 1;
    }

    strcpy(request.ifr_name, argv[1]);
    strcpy(request.ifr_newname, argv[2]);
    if (ioctl(socket_fd, SIOCSIFNAME, &request) < 0) {
        perror("SIOCSIFNAME");
        close(socket_fd);
        return 1;
    }

    close(socket_fd);
    return 0;
}
