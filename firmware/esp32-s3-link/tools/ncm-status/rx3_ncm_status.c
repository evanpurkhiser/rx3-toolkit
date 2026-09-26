#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define DIAGNOSTIC_ETHERTYPE 0x88b5
#define DIAGNOSTIC_REQUEST "RX3STAT?"
#define DIAGNOSTIC_RESPONSE "RX3STAT1"
#define BOOTLOADER_REQUEST "RX3BOOT?"
#define BOOTLOADER_RESPONSE "RX3BOOT1"

static void fail(const char *operation)
{
    perror(operation);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s INTERFACE [status|bootloader]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *mode = argc == 3 ? argv[2] : "status";
    const char *request_text = DIAGNOSTIC_REQUEST;
    const char *response_text = DIAGNOSTIC_RESPONSE;
    if (strcmp(mode, "bootloader") == 0) {
        request_text = BOOTLOADER_REQUEST;
        response_text = BOOTLOADER_RESPONSE;
    } else if (strcmp(mode, "status") != 0) {
        fprintf(stderr, "unknown mode: %s\n", mode);
        return EXIT_FAILURE;
    }

    const int descriptor = socket(AF_PACKET, SOCK_RAW, htons(DIAGNOSTIC_ETHERTYPE));
    if (descriptor < 0) {
        fail("socket");
    }

    struct ifreq interface = {0};
    snprintf(interface.ifr_name, sizeof(interface.ifr_name), "%s", argv[1]);

    if (ioctl(descriptor, SIOCGIFINDEX, &interface) < 0) {
        fail("SIOCGIFINDEX");
    }
    const int interface_index = interface.ifr_ifindex;

    if (ioctl(descriptor, SIOCGIFHWADDR, &interface) < 0) {
        fail("SIOCGIFHWADDR");
    }

    unsigned char request[ETH_FRAME_LEN] = {0};
    struct ethhdr *ethernet = (struct ethhdr *)request;
    memset(ethernet->h_dest, 0xff, ETH_ALEN);
    memcpy(ethernet->h_source, interface.ifr_hwaddr.sa_data, ETH_ALEN);
    ethernet->h_proto = htons(DIAGNOSTIC_ETHERTYPE);
    const size_t payload_length = strlen(request_text);
    memcpy(request + ETH_HLEN, request_text, payload_length);

    struct sockaddr_ll address = {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(DIAGNOSTIC_ETHERTYPE),
        .sll_ifindex = interface_index,
        .sll_halen = ETH_ALEN,
    };
    memset(address.sll_addr, 0xff, ETH_ALEN);

    const size_t request_length = ETH_ZLEN;
    if (sendto(descriptor,
               request,
               request_length,
               0,
               (struct sockaddr *)&address,
               sizeof(address)) != (ssize_t)request_length) {
        fail("sendto");
    }

    struct pollfd poll_descriptor = {
        .fd = descriptor,
        .events = POLLIN,
    };

    for (;;) {
        const int ready = poll(&poll_descriptor, 1, 2000);
        if (ready < 0) {
            fail("poll");
        }
        if (ready == 0) {
            fprintf(stderr, "timed out waiting for S3 status\n");
            return EXIT_FAILURE;
        }

        unsigned char response[ETH_FRAME_LEN + 1];
        const ssize_t length = recv(descriptor, response, ETH_FRAME_LEN, 0);
        if (length < 0) {
            fail("recv");
        }
        const size_t response_length = strlen(response_text);
        if (length < ETH_HLEN + (ssize_t)response_length ||
            memcmp(response + ETH_HLEN,
                   response_text,
                   response_length) != 0) {
            continue;
        }

        response[length] = '\0';
        puts((char *)response + ETH_HLEN);
        return EXIT_SUCCESS;
    }
}
