#define _POSIX_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <wayland-client.h>

#include "protocol/wlr-data-control-unstable-v1-client-protocol.h"

#define PORT 2653

#define TXT_MIMETYPE "text/plain;charset=utf-8"

char clipboard_cur[BUFSIZ];
ssize_t clipboard_cur_len = 0;
bool have_seat = false;
int pipes[2];
int sockfd;

struct wl_seat *seat = NULL;
struct wl_display *display = NULL;
struct zwlr_data_control_manager_v1 *data_control_manager = NULL;
struct zwlr_data_control_offer_v1 *accepted_offer = NULL;

void registry_global_handler(void *data, struct wl_registry *reg, uint32_t name, const char *iface, uint32_t ver)
{
    if (!have_seat && !strcmp(iface, "wl_seat")) {
        seat = wl_registry_bind(reg, name, &wl_seat_interface, 2);
        have_seat = true;
    } else if (!strcmp(iface, "zwlr_data_control_manager_v1")) {
        data_control_manager = wl_registry_bind(reg, name, &zwlr_data_control_manager_v1_interface, 2);
    }
}

void registry_global_remove_handler(void *data, struct wl_registry *reg, uint32_t name)
{
}

struct wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler,
};

void offer_handler(void *data, struct zwlr_data_control_offer_v1 *offer, const char *mimetype)
{
    if (accepted_offer == NULL && !strcmp(mimetype, TXT_MIMETYPE)) {
        accepted_offer = offer;
    }
}

struct zwlr_data_control_offer_v1_listener offer_listener = {
    .offer = offer_handler,
};

void data_control_offer_handler(void *data, struct zwlr_data_control_device_v1 *dev, struct zwlr_data_control_offer_v1 *offer)
{
    zwlr_data_control_offer_v1_add_listener(offer, &offer_listener, NULL);
}

void data_control_selection_handler(void *data, struct zwlr_data_control_device_v1 *dev, struct zwlr_data_control_offer_v1 *offer)
{
    if (offer) {
        if (offer == accepted_offer) {
            zwlr_data_control_offer_v1_receive(offer, TXT_MIMETYPE, pipes[1]);
            wl_display_roundtrip(display);

            memset(clipboard_cur, 0, BUFSIZ);
            clipboard_cur_len = read(pipes[0], clipboard_cur, BUFSIZ);
            if (clipboard_cur_len == -1) {
                fprintf(stderr, "failed to read clipboard data\n");
            }

            accepted_offer = NULL;
        } else {
            zwlr_data_control_offer_v1_destroy(offer);
        }
    }
}

void data_control_primary_selection_handler(void *data, struct zwlr_data_control_device_v1 *dev, struct zwlr_data_control_offer_v1 *offer)
{
    if (offer) {
        if (offer == accepted_offer) {
            accepted_offer = NULL;
        }

        zwlr_data_control_offer_v1_destroy(offer);
    }
}

struct zwlr_data_control_device_v1_listener device_listener = {
    .data_offer = data_control_offer_handler,
    .selection = data_control_selection_handler,
    .primary_selection = data_control_primary_selection_handler,
};

int init_wl_client()
{
    display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "could not connect to wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    if (registry == NULL) {
        fprintf(stderr, "could not get registry\n");
        return 1;
    }

    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_roundtrip(display);

    if (seat == NULL) {
        fprintf(stderr, "could not get seat\n");
        return 1;
    }

    if (data_control_manager == NULL) {
        fprintf(stderr, "failed to bind to data_control_manager interface\n");
        return 1;
    }

    if (pipe(pipes) == -1) {
        fprintf(stderr, "failed to create pipes\n");
        return 1;
    }

    struct zwlr_data_control_device_v1 *dev = zwlr_data_control_manager_v1_get_data_device(data_control_manager, seat);
    if (dev == NULL) {
        fprintf(stderr, "device is null\n");
        return 1;
    }

    zwlr_data_control_device_v1_add_listener(dev, &device_listener, NULL);
    wl_display_roundtrip(display);

    return 0;
}

int init_socket() {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        fprintf(stderr, "Could not open socket\n");
        return 1;
    }

    const int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1) {
        fprintf(stderr, "Failed to set socket option\n");
        return 1;
    }

    /*
    if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) | O_NONBLOCK) == -1) {
        fprintf(stderr, "Failed to set the socket to be non-blocking\n");
        return 1;
    }
    */

    const struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        // NEVER bind to anything OTHER than localhost
        //  we do not want to accidentally expose our clipboard to the internet...
        .sin_addr.s_addr = inet_addr("127.0.0.1"),
    };

    if (bind(sockfd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) == -1) {
        fprintf(stderr, "Failed to bind socket\n");
        return 1;
    }

    if (listen(sockfd, 64) == -1) {
        fprintf(stderr, "Failed to start listening\n");
        return 1;
    }

    return 0;
}

int http_accept()
{
    struct sockaddr_in client_addr;
    socklen_t clientaddr_len;

    int clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &clientaddr_len);
    if (clientfd == -1) {
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }

    printf("Incoming connection from %i.%i.%i.%i\n",
            client_addr.sin_addr.s_addr & 0xFF,
            (client_addr.sin_addr.s_addr >> 8) & 0xFF,
            (client_addr.sin_addr.s_addr >> 16) & 0xFF,
            (client_addr.sin_addr.s_addr >> 24) & 0xFF);

    return clientfd;
}

int main(int argc, char **argv)
{
    int status = 0;

    if ((status = init_wl_client()) != 0) {
        return status;
    }

    if ((status = init_socket()) != 0) {
        goto cleanup;
    }

    printf("Started listening on port %i\n", PORT);

    FILE *f;
    int clientfd;

    do
    {
        if ((clientfd = http_accept()) > 0) {
            wl_display_roundtrip(display);

            f = fdopen(clientfd, "w+");

            fprintf(f, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n%s", clipboard_cur);
            fflush(f);
            shutdown(clientfd, SHUT_RDWR);
            close(clientfd);
        }

    }
    while (wl_display_dispatch_pending(display) != -1);

cleanup:
    close(sockfd);
    wl_display_disconnect(display);
    close(pipes[0]);
    close(pipes[1]);

    return status;
}
