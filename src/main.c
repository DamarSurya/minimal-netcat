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

#define DEFAULT_ADDRESS ((const char *)"0.0.0.0")
#define DEFAULT_PORT ((uint16_t)3000)
#define MAX_BUFFER_SIZE ((size_t)4096)
#define USAGE_MESSAGE                                                          \
    (const char                                                                \
         *)"Usage:\t ./[program] [-l (listen mode) ] [ [ip] [port] (sending mode) ] \n\tOptions:\n\
    \t\t-v: \tSet verbose mode\n\
    \t\t-l: \tSet to listen mode\n\
    \t\t-p: \tSet port\n\
    \t\t-i: \tSet ip\n\
    \t\t-6: \tEnable ipv6\n\
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
    bool is_ipv6 = false;
    bool message_is_set = false;
    bool listen_is_looped = false;
    const char *address = DEFAULT_ADDRESS;
    uint16_t port = htons(DEFAULT_PORT);
    const char *message_ptr = "\0";

    int domain = AF_INET;
    int socket_type = SOCK_STREAM;
    int protocol = IPPROTO_TCP;
    int max_backlog = 10;
    int enableREUSEADDR = 1;

    opterr = 0;
    int opt, nargs = 0;
    while ((opt = getopt(argc, argv, "lhvri:p:m:b:6")) != -1) {
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
            address = optarg;
            break;
        case 'p':
            port = htons((uint16_t)atoi(optarg));
            break;
        case '6':
            is_ipv6 = true;
            domain = AF_INET6;
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
        struct sockaddr_storage local_socket_address;
        socklen_t socket_size;
        if (is_ipv6) {
            struct sockaddr_in6 *addr =
                (struct sockaddr_in6 *)&local_socket_address;
            if (inet_pton(AF_INET6, address, &addr->sin6_addr) == -1)
                sys_err("inet_pton");
            addr->sin6_family = domain;
            addr->sin6_port = port;
            socket_size = sizeof(struct sockaddr_in6);
        } else {
            struct sockaddr_in *addr =
                (struct sockaddr_in *)&local_socket_address;
            addr->sin_addr.s_addr = inet_addr(address);
            addr->sin_port = port;
            addr->sin_family = domain;
            socket_size = sizeof(struct sockaddr_in);
        }
        if ((local_socket = socket(domain, socket_type, protocol)) == -1)
            sys_err("socket");
        if (setsockopt(local_socket, SOL_SOCKET, SO_REUSEADDR,
                       (void *)&enableREUSEADDR,
                       (socklen_t)sizeof(enableREUSEADDR)) == -1)
            sys_err("setsockopt");
        if (bind(local_socket, (struct sockaddr *)&local_socket_address,
                 socket_size) == -1)
            sys_err("bind");
        if (listen(local_socket, max_backlog) == -1)
            sys_err("listen");
        flog_verb("[LOG]: Running on: \n\tIP Mode: %s\n\tIPPROTO: %s\n\tIp: "
                  "%s\n\tPort: %d\n\tMax_backlog: "
                  "%d\n\tmessage "
                  "length: %lu\n\n",
                  ((is_ipv6) ? "IPV6" : "IPV4"), ("TCP/IP"), address,
                  ntohs(port), max_backlog, strlen(message_ptr));
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
            log_verb("Accepting connection");
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
    log_verb("On Sending Mode");
    if (argc < 3) {
        fprintf(stderr, "[ERROR]: Not enough arguments\n%s", USAGE_MESSAGE);
        return -1;
    }
    address = argv[argc - 2];
    port = htons(atoi(argv[argc - 1]));
    int local_socket = INACTIVE_SOCKET;
    struct sockaddr_storage target_socket_addr;
    socklen_t socket_size;
    if (is_ipv6) {
        struct sockaddr_in6 *addr = (struct sockaddr_in6 *)&target_socket_addr;
        if (inet_pton(domain, address, &addr->sin6_addr) == -1)
            sys_err("inet_pton");
        addr->sin6_family = domain;
        addr->sin6_port = port;
        socket_size = sizeof(struct sockaddr_in6);
    } else {
        struct sockaddr_in *addr = (struct sockaddr_in *)&target_socket_addr;
        addr->sin_addr.s_addr = inet_addr(address);
        addr->sin_port = port;
        addr->sin_family = domain;
        socket_size = sizeof(struct sockaddr_in);
    }
    if ((local_socket = socket(domain, socket_type, protocol)) == -1)
        sys_err("socket");
    flog_verb("\tIP Mode: %s\n\tIPPROTO: %s\n\tConnecting to: %s:%s\n\n",
              ((is_ipv6) ? "IPV6" : "IPV4"), ("TCP/IP"), address,
              argv[argc - 1]);
    if (connect(local_socket, (struct sockaddr *)&target_socket_addr,
                socket_size) == -1)
        sys_err("connect");
    log_verb("Connection established.");

    if (message_is_set) {
        flog_verb("[LOG]: Sending message. length: %lu\n", strlen(message_ptr));
        send_buffer(local_socket, message_ptr, strlen(message_ptr), 0);
    }
    if (isatty(STDIN_FILENO) == 0) {
        struct stat buf_info;
        fstat(STDIN_FILENO, &buf_info);
        flog_verb("[LOG]: Sending file. File size: %lu\n", buf_info.st_size);
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
