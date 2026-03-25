#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/sendfile.h>
#include <sys/socket.h>

#include <argp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_ADDRESS ((in_addr_t)INADDR_ANY)
#define DEFAULT_PORT ((uint16_t)3000)
#define MAX_BUFFER_SIZE ((size_t)4096)
#define USAGE_MESSAGE                                                          \
    (const char                                                                \
         *)"Usage:\t ./[program] [-l (listen mode) ] [ [ip] [port] (sending mode) ] \n\tOptions:\n\
    \t\t-v: \tSet verbose mode\n\
    \t\t-l: \tSet to listen mode\n\
    \t\t-p: \tSet port\n\
    \t\t-i: \tSet ip\n\
    \t\t-r: \tLooped listen mode\n\
    \t\t-m: \tSet message\n\
    \t\t-b: \tSet max_backlog or maximum connection\n\
    \tRedirection: ./[program] [-l > out] [ [ip] [port] < in]\n"
#define INACTIVE_SOCKET ((int)-42)

bool listen_mode = false;
bool verbose_mode = false;

#define sys_err(err)                                                           \
    do {                                                                       \
        fprintf(stderr, "[ERROR]: ");                                          \
        perror(err);                                                           \
        exit(-1);                                                              \
    } while (0);
#define log(msg)                                                               \
    do {                                                                       \
        fprintf(stdout, "%s\n", msg);                                          \
    } while (0);
#define flog(format, ...)                                                      \
    do {                                                                       \
        fprintf(stdout, format, __VA_ARGS__);                                  \
    } while (0);
#define log_verb(msg)                                                          \
    do {                                                                       \
        if (verbose_mode)                                                      \
            fprintf(stderr, "[LOG]: %s\n", msg);                               \
    } while (0);
#define flog_verb(format, ...)                                                 \
    do {                                                                       \
        if (verbose_mode)                                                      \
            fprintf(stderr, format, __VA_ARGS__);                              \
    } while (0);
int send_buffer(int fd, const char *buffer, size_t n, int flags);
int send_file(int out_fd, int in_fd, size_t count);
int recv_buffer(int fd, char *buffer, size_t n, int flags);

int main(int argc, char **argv) {
    if (argc <= 1) {
        fprintf(stderr, "[ERROR]: Not enough arguments\n%s", USAGE_MESSAGE);
        exit(-1);
    }
    bool ip_is_set = false;
    bool port_is_set = false;
    bool message_is_set = false;
    bool listen_is_looped = false;

    in_addr_t local_address = DEFAULT_ADDRESS;
    uint16_t local_port = htons(DEFAULT_PORT);
    const char *message_ptr = "\0";

    int domain = AF_INET;
    int socket_type = SOCK_STREAM;
    int protocol = 0;
    int max_backlog = 10;
    int enableREUSEADDR = 1;

    opterr = 0;
    int opt, nargs = 0;
    while ((opt = getopt(argc, argv, "lhvri:p:m:b:")) != -1) {
        ++nargs;
        switch (opt) {
        case 'h':
            fprintf(stderr, USAGE_MESSAGE);
            exit(1);
            break;
        case 'm':
            message_ptr = optarg;
            message_is_set = true;
            break;
        case 'v':
            verbose_mode = true;
            break;
        case 'l':
            listen_mode = true;
            break;
        case 'r':
            listen_is_looped = true;
            break;
        case 'i':
            ip_is_set = true;
            local_address = inet_addr(optarg);
            break;
        case 'p':
            port_is_set = true;
            local_port = htons((uint16_t)atoi(optarg));
            break;
        case 'b':
            max_backlog = atoi(optarg);
            break;
        default:
            break;
        }
    }

    if (listen_mode) {
        log_verb("[LOG]: On listen mode\n");
        int local_socket = INACTIVE_SOCKET;
        struct sockaddr_in local_socket_address;
        local_socket_address.sin_addr.s_addr = local_address;
        local_socket_address.sin_port = local_port;
        local_socket_address.sin_family = domain;
        if ((local_socket = socket(domain, socket_type, protocol)) == -1)
            sys_err("socket");
        if (setsockopt(local_socket, SOL_SOCKET, SO_REUSEADDR,
                       (void *)&enableREUSEADDR,
                       (socklen_t)sizeof(enableREUSEADDR)) == -1)
            sys_err("setsockopt");
        if (bind(local_socket, (struct sockaddr *)&local_socket_address,
                 sizeof(local_socket_address)) == -1)
            sys_err("bind");
        if (listen(local_socket, max_backlog) == -1)
            sys_err("listen");
        flog_verb("[LOG]: Running on: \n\tip: %s\n\tport: %d\n\tmessage "
                  "length: %lu\n",
                  inet_ntoa(local_socket_address.sin_addr), ntohs(local_port),
                  strlen(message_ptr));

        while (1) {
            int peer_socket = INACTIVE_SOCKET;
            struct sockaddr peer_socket_addr;
            struct sockaddr_in *peer_addr;
            socklen_t peer_socklen;

            log_verb("[LOG]: listening...");
            if ((peer_socket = accept(local_socket, &peer_socket_addr,
                                      &peer_socklen)) == -1)
                sys_err("accept");
            peer_addr = (struct sockaddr_in *)&peer_socket_addr;
            char buffer[MAX_BUFFER_SIZE];
            int len = recv_buffer(peer_socket, buffer, MAX_BUFFER_SIZE, 0);
            log_verb("[LOG]: Accepting connection");
            flog_verb("[LOG]: Connection info: \n\tip: %s\n\tport: "
                      "%u\n\tmessage length received: %d\n\n",
                      inet_ntoa(peer_addr->sin_addr),
                      ntohs(peer_addr->sin_port), len);

            shutdown(peer_socket, SHUT_RD);
            if (message_is_set) {
                log_verb("sending message...");
                send_buffer(peer_socket, message_ptr, strlen(message_ptr), 0);
            }
            shutdown(peer_socket, SHUT_WR);
            close(peer_socket);
            if (!listen_is_looped)
                break;
        }
        close(local_socket);
        return 0;
    }

    // sending mode
    log_verb("On sending mode");
    if (argc < 3) {
        fprintf(stderr, "[ERROR]: Not enough arguments\n%s", USAGE_MESSAGE);
        return -1;
    }
    int local_socket = INACTIVE_SOCKET;
    struct sockaddr_in target_socket_addr;
    target_socket_addr.sin_addr.s_addr = inet_addr(argv[nargs + 1]);
    target_socket_addr.sin_port = htons(atoi(argv[nargs + 2]));
    target_socket_addr.sin_family = domain;
    if ((local_socket = socket(domain, socket_type, protocol)) == -1)
        sys_err("socket");
    flog_verb("[LOG]: Connecting to: %s:%s\n",
              inet_ntoa(target_socket_addr.sin_addr), argv[3]);
    if (connect(local_socket, (struct sockaddr *)&target_socket_addr,
                sizeof(target_socket_addr)) == -1)
        sys_err("connect");
    log_verb("Connection established.");

    if (message_is_set) {
        flog_verb("Sending message. len: %lu\n", strlen(message_ptr));
        send_buffer(local_socket, message_ptr, strlen(message_ptr), 0);
    }
    if (isatty(STDIN_FILENO) == 0) {
        struct stat buf_info;
        fstat(STDIN_FILENO, &buf_info);
        flog_verb("Sending file. File size: %lu\n", buf_info.st_size);
        send_file(local_socket, 0, buf_info.st_size);
    }
    shutdown(local_socket, SHUT_WR);
    close(local_socket);
    return 0;
}

int send_buffer(int fd, const char *buffer, size_t n, int flags) {
    size_t len = n;
    ssize_t total = 0, sent = 0;
    while (len > 0) {
        if ((sent = send(fd, buffer + total, len, flags)) == -1)
            sys_err("send");
        total += sent;
        len -= (size_t)sent;
    }
    return (int)total;
}
int send_file(int out_fd, int in_fd, size_t count) {
    off_t offset = 0;
    size_t len = count;
    ssize_t total = 0, sent = 0;
    while (len > 0) {
        if ((sent = sendfile(out_fd, in_fd, &offset, len)) == -1)
            sys_err("send_file");
        total += sent;
        len -= (size_t)sent;
    }
    return (int)total;
}
int recv_buffer(int fd, char *buffer, size_t n, int flags) {
    ssize_t total = 0, nrecv = 0;
    while ((nrecv = recv(fd, buffer, n, flags)) != 0) {
        if (nrecv == -1)
            sys_err("recv");
        total += nrecv;
        fprintf(stdout, "%.*s", (int)nrecv, buffer);
        if (strstr(buffer, "\r\n\r\n") != NULL)
            break;
    }
    fflush(stdout);
    return (int)total;
}
