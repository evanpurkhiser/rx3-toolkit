// SPDX-License-Identifier: MPL-2.0

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
#include <termios.h>
#include <unistd.h>

#include "../../include/rx3_s3_config_protocol.h"

#define FRAME_CAPACITY 256

typedef struct {
    int descriptor;
    int interface_index;
    unsigned char mac[ETH_ALEN];
} endpoint_t;

static void fail(const char *operation)
{
    perror(operation);
    exit(EXIT_FAILURE);
}

static void wipe(void *memory, size_t length)
{
    volatile unsigned char *bytes = memory;
    while (length-- != 0) {
        *bytes++ = 0;
    }
}

static endpoint_t open_endpoint(const char *interface_name)
{
    endpoint_t endpoint = {
        .descriptor = socket(AF_PACKET, SOCK_RAW, htons(RX3_S3_CONFIG_ETHERTYPE)),
    };
    if (endpoint.descriptor < 0) {
        fail("socket");
    }

    struct ifreq interface = {0};
    snprintf(interface.ifr_name, sizeof(interface.ifr_name), "%s", interface_name);
    if (ioctl(endpoint.descriptor, SIOCGIFINDEX, &interface) < 0) {
        fail("SIOCGIFINDEX");
    }
    endpoint.interface_index = interface.ifr_ifindex;
    if (ioctl(endpoint.descriptor, SIOCGIFHWADDR, &interface) < 0) {
        fail("SIOCGIFHWADDR");
    }
    memcpy(endpoint.mac, interface.ifr_hwaddr.sa_data, sizeof(endpoint.mac));

    struct sockaddr_ll bind_address = {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(RX3_S3_CONFIG_ETHERTYPE),
        .sll_ifindex = endpoint.interface_index,
    };
    if (bind(endpoint.descriptor,
             (struct sockaddr *)&bind_address,
             sizeof(bind_address)) < 0) {
        fail("bind");
    }
    return endpoint;
}

static size_t write_header(unsigned char *payload,
                           unsigned char opcode,
                           unsigned short request_id,
                           size_t body_length)
{
    memcpy(payload, "RX3C", 4);
    payload[4] = RX3_S3_CONFIG_VERSION;
    payload[5] = opcode;
    payload[6] = RX3_S3_CONFIG_OK;
    payload[7] = 0;
    payload[8] = request_id >> 8;
    payload[9] = request_id & 0xff;
    payload[10] = body_length >> 8;
    payload[11] = body_length & 0xff;
    return RX3_S3_CONFIG_HEADER_SIZE;
}

static size_t build_request(unsigned char *frame,
                            const endpoint_t *endpoint,
                            unsigned char opcode,
                            unsigned short request_id,
                            const char *ssid,
                            size_t ssid_length,
                            const char *password,
                            size_t password_length)
{
    memset(frame, 0, FRAME_CAPACITY);
    memset(frame, 0xff, ETH_ALEN);
    memcpy(frame + ETH_ALEN, endpoint->mac, ETH_ALEN);
    frame[12] = RX3_S3_CONFIG_ETHERTYPE >> 8;
    frame[13] = RX3_S3_CONFIG_ETHERTYPE & 0xff;

    const size_t body_length = opcode == RX3_S3_CONFIG_SET_CREDENTIALS
                                   ? 2 + ssid_length + password_length
                                   : 0;
    unsigned char *payload = frame + ETH_HLEN;
    write_header(payload, opcode, request_id, body_length);
    if (body_length != 0) {
        payload[12] = ssid_length;
        payload[13] = password_length;
        memcpy(payload + 14, ssid, ssid_length);
        memcpy(payload + 14 + ssid_length, password, password_length);
    }
    const size_t length = ETH_HLEN + RX3_S3_CONFIG_HEADER_SIZE + body_length;
    return length < ETH_ZLEN ? ETH_ZLEN : length;
}

static size_t exchange(const endpoint_t *endpoint,
                       unsigned char opcode,
                       const char *ssid,
                       size_t ssid_length,
                       const char *password,
                       size_t password_length,
                       unsigned char response[FRAME_CAPACITY])
{
    static unsigned short next_request_id = 1;
    const unsigned short request_id = next_request_id++;
    unsigned char request[FRAME_CAPACITY];
    const size_t request_length = build_request(
        request, endpoint, opcode, request_id,
        ssid, ssid_length, password, password_length);
    struct sockaddr_ll destination = {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(RX3_S3_CONFIG_ETHERTYPE),
        .sll_ifindex = endpoint->interface_index,
        .sll_halen = ETH_ALEN,
        .sll_addr = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
    };
    const ssize_t sent = sendto(endpoint->descriptor, request, request_length, 0,
                                (struct sockaddr *)&destination,
                                sizeof(destination));
    wipe(request, sizeof(request));
    if (sent != (ssize_t)request_length) {
        fail("sendto");
    }

    struct pollfd wait = {.fd = endpoint->descriptor, .events = POLLIN};
    while (poll(&wait, 1, 2000) > 0) {
        const ssize_t received = recv(endpoint->descriptor,
                                      response, FRAME_CAPACITY, 0);
        if (received < ETH_HLEN + RX3_S3_CONFIG_HEADER_SIZE) {
            continue;
        }
        const unsigned char *payload = response + ETH_HLEN;
        if (response[12] != (RX3_S3_CONFIG_ETHERTYPE >> 8) ||
            response[13] != (RX3_S3_CONFIG_ETHERTYPE & 0xff) ||
            memcmp(payload, "RX3C", 4) != 0 ||
            payload[4] != RX3_S3_CONFIG_VERSION ||
            payload[5] != (opcode | RX3_S3_CONFIG_RESPONSE_BIT) ||
            payload[7] != 0 ||
            payload[8] != (request_id >> 8) ||
            payload[9] != (request_id & 0xff)) {
            continue;
        }
        const size_t declared = ((size_t)payload[10] << 8) | payload[11];
        if (declared > (size_t)received - ETH_HLEN - RX3_S3_CONFIG_HEADER_SIZE) {
            continue;
        }
        if (payload[6] != RX3_S3_CONFIG_OK) {
            fprintf(stderr, "S3 rejected command with status %u\n", payload[6]);
            exit(EXIT_FAILURE);
        }
        return declared;
    }
    fprintf(stderr, "timed out waiting for S3 response\n");
    exit(EXIT_FAILURE);
}

static void print_status(const endpoint_t *endpoint)
{
    unsigned char response[FRAME_CAPACITY];
    const size_t length = exchange(endpoint, RX3_S3_CONFIG_GET_STATUS,
                                   NULL, 0, NULL, 0, response);
    const unsigned char *body = response + ETH_HLEN + RX3_S3_CONFIG_HEADER_SIZE;
    if (length < RX3_S3_CONFIG_STATUS_FIXED_SIZE ||
        body[8] > RX3_S3_CONFIG_SSID_MAX_LENGTH ||
        length != (size_t)RX3_S3_CONFIG_STATUS_FIXED_SIZE + body[8]) {
        fprintf(stderr, "invalid status payload\n");
        exit(EXIT_FAILURE);
    }
    char ssid[RX3_S3_CONFIG_SSID_MAX_LENGTH + 1] = {0};
    memcpy(ssid, body + RX3_S3_CONFIG_STATUS_FIXED_SIZE, body[8]);
    printf("wifi=%s ncm=%s rssi=%d credentials=%s password=%s override=%s "
           "mac=%02x:%02x:%02x:%02x:%02x:%02x ssid=%s\n",
           body[0] & RX3_S3_CONFIG_FLAG_WIFI_CONNECTED ? "connected" : "disconnected",
           body[0] & RX3_S3_CONFIG_FLAG_NCM_LINK_UP ? "up" : "down",
           (signed char)body[1],
           body[0] & RX3_S3_CONFIG_FLAG_CREDENTIALS_CONFIGURED ? "set" : "unset",
           body[0] & RX3_S3_CONFIG_FLAG_PASSWORD_SET ? "set" : "unset",
           body[0] & RX3_S3_CONFIG_FLAG_SAVED_OVERRIDE ? "saved" : "fallback",
           body[2], body[3], body[4], body[5], body[6], body[7], ssid);
}

static size_t read_password(char password[RX3_S3_CONFIG_PASSWORD_MAX_LENGTH + 2])
{
    struct termios original;
    const int terminal = isatty(STDIN_FILENO) &&
                         tcgetattr(STDIN_FILENO, &original) == 0;
    if (terminal) {
        struct termios hidden = original;
        hidden.c_lflag &= (tcflag_t)~ECHO;
        fprintf(stderr, "Wi-Fi password (empty for open network): ");
        fflush(stderr);
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) < 0) {
            fail("tcsetattr");
        }
    }
    if (fgets(password, RX3_S3_CONFIG_PASSWORD_MAX_LENGTH + 2, stdin) == NULL) {
        if (terminal) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
        }
        fprintf(stderr, "could not read password\n");
        exit(EXIT_FAILURE);
    }
    if (terminal) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
        fputc('\n', stderr);
    }
    size_t length = strcspn(password, "\r\n");
    if (password[length] == '\0' && length > RX3_S3_CONFIG_PASSWORD_MAX_LENGTH) {
        fprintf(stderr, "password is too long\n");
        exit(EXIT_FAILURE);
    }
    password[length] = '\0';
    return length;
}

static void set_credentials(const endpoint_t *endpoint, const char *ssid)
{
    const size_t ssid_length = strlen(ssid);
    if (ssid_length == 0 || ssid_length > RX3_S3_CONFIG_SSID_MAX_LENGTH) {
        fprintf(stderr, "SSID must contain 1-%u bytes\n",
                RX3_S3_CONFIG_SSID_MAX_LENGTH);
        exit(EXIT_FAILURE);
    }
    char password[RX3_S3_CONFIG_PASSWORD_MAX_LENGTH + 2] = {0};
    const size_t password_length = read_password(password);
    if (password_length > 0 && password_length < 8) {
        wipe(password, sizeof(password));
        fprintf(stderr, "password must be empty or contain 8-%u bytes\n",
                RX3_S3_CONFIG_PASSWORD_MAX_LENGTH);
        exit(EXIT_FAILURE);
    }
    unsigned char response[FRAME_CAPACITY];
    (void)exchange(endpoint, RX3_S3_CONFIG_SET_CREDENTIALS,
                   ssid, ssid_length, password, password_length, response);
    wipe(password, sizeof(password));
    puts("credentials saved; S3 reconnect requested");
}

static void reconnect(const endpoint_t *endpoint)
{
    unsigned char response[FRAME_CAPACITY];
    (void)exchange(endpoint, RX3_S3_CONFIG_RECONNECT,
                   NULL, 0, NULL, 0, response);
    puts("S3 reconnect requested");
}


static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s INTERFACE status\n"
            "       %s INTERFACE set SSID\n"
            "       %s INTERFACE reconnect\n",
            program, program, program);
}

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    endpoint_t endpoint = open_endpoint(argv[1]);
    if (argc == 3 && strcmp(argv[2], "status") == 0) {
        print_status(&endpoint);
    } else if (argc == 4 && strcmp(argv[2], "set") == 0) {
        set_credentials(&endpoint, argv[3]);
    } else if (argc == 3 && strcmp(argv[2], "reconnect") == 0) {
        reconnect(&endpoint);
    } else {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    close(endpoint.descriptor);
    return EXIT_SUCCESS;
}
