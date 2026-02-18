#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define LogInfo(v)                                                             \
    do {                                                                       \
        fprintf(stdout, "[INFO]: %s\n", v);                                    \
    } while (0);
#define LogError(v)                                                            \
    do {                                                                       \
        fprintf(stderr, "%s", v);                                              \
    } while (0);
#define SysErr(err)                                                            \
    do {                                                                       \
        perror(err);                                                           \
        exit(-1);                                                              \
    } while (0);

#define REDIR_STDIN 0             // redirected stdin
#define REDIR_STDOUT 0            // reedirected stdout
#define MAX_BACKLOG 1             // max backlog
#define MAXBUF 4096 // max in buffer size/stored buffer
                
#define DEFAULT_ADDRESS "0.0.0.0" // default addresss
#define SOCK_TYPE SOCK_STREAM     // default socket type
#define DEFAULT_PORT 3000         // default port
#define DOMAIN AF_INET            // default value for socket domain/family

// socket opt
const int enableREUSEADDR = 1;

enum {
    SENDING_MODE, // sending mode
    LISTEN_MODE   // listen mode
};

int MODE = SENDING_MODE;
int LISTEN_LOOPED = false;
bool IP_IS_SET = false;
bool PORT_IS_SET = false;

struct in_addr inAddr;
struct sockaddr_in sockAddr;
char *addressStr;
int port;

int CreateSocket(int domain, int type, int proto) {
    int ec;
    if ((ec = socket(domain, type, proto)) == -1)
        SysErr("socket");
    return ec;
}

// send data from FILE* stream
// return total sent bytes
// auto exit on error via SysErr
ssize_t SendFile(int socket, FILE *stream, int flags);

// return total bytes received
// auto write to stdout
// flush the stdout buffer after receive
// auto exit on error via SysErr
ssize_t Recv(int socket, int flags);

int main(int argc, char **argv) {
    const char *USAGE_MESSAGE =
        "Usage:\t ./[program] [-l (listen mode) ] [ [ip] [port] (sending mode) ] \n\tOptions:\n\
    \t\t-l: \tSet to listen mode\n\
    \t\t-p: \tSet port\n\
    \t\t-i: \tSet ip\n\
    \t\t-r: \tLooped listen mode\n\
    \tRedirection: ./[program] [-l > out] [ 0.0.0.0 3100 < in]\n";
    if (argc <= 1) {
        LogError(USAGE_MESSAGE);
        return 1;
    }
    opterr = 0;
    int opt;
    while ((opt = getopt(argc, argv, "lh")) != -1) {
        switch (opt) {
        case 'h':
            fprintf(stderr, "%s", USAGE_MESSAGE);
            exit(1);
            break;
        case 'l':
            MODE = LISTEN_MODE;
            while ((opt = getopt(argc, argv, "ri:p:")) != -1) {
                switch (opt) {
                case 'r':
                    LISTEN_LOOPED = true;
                    break;
                case 'i':
                    addressStr = optarg;
                    IP_IS_SET = true;
                    break;
                case 'p':
                    port = atoi(optarg);
                    PORT_IS_SET = true;
                    break;
                default:
                    break;
                }
            }
            break;
        default:
            break;
        }
    }
    if (MODE == SENDING_MODE) {
        if (argc < 3) {
            LogError(USAGE_MESSAGE);
            return 1;
        }
        addressStr = argv[1];
        port = atoi(argv[2]);

    } else if (MODE == LISTEN_MODE) {
        if (!IP_IS_SET)
            addressStr = DEFAULT_ADDRESS;
        if (!PORT_IS_SET)
            port = DEFAULT_PORT;
    }
    // initialize inAddr and assign value to sockAddr
    if (inet_pton(DOMAIN, addressStr, &inAddr) == -1)
        SysErr("inet_pton");
    sockAddr.sin_addr = inAddr;
    sockAddr.sin_family = DOMAIN;
    sockAddr.sin_port = htons(port);
    if (MODE == SENDING_MODE) {
        int socket;
        socket = CreateSocket(DOMAIN, SOCK_TYPE, 0);

        if (connect(socket, (struct sockaddr *)&sockAddr, sizeof(sockAddr)) ==
            -1)
            SysErr("connect");
        if (isatty(STDIN_FILENO) == REDIR_STDIN) {
            ssize_t totalSent = SendFile(socket, stdin, 0);
            shutdown(socket, SHUT_WR);
        }
        close(socket);
    } else if (MODE == LISTEN_MODE) {
        int localSocket;
        socklen_t localSockSize;
        localSocket = CreateSocket(DOMAIN, SOCK_TYPE, 0);
        localSockSize = sizeof(sockAddr);
        setsockopt(localSocket, SOL_SOCKET, SO_REUSEADDR, &enableREUSEADDR,
                   sizeof(enableREUSEADDR));

        if (bind(localSocket, (struct sockaddr *)&sockAddr, localSockSize) ==
            -1) {
            SysErr("bind");
        }
        if (listen(localSocket, MAX_BACKLOG) == -1) {
            SysErr("listen");
        }
        while (1) {
            int peerSocket;
            if ((peerSocket = accept(localSocket, NULL, NULL)) == -1) {
                SysErr("accept");
            }
            ssize_t totalReceived = Recv(peerSocket, 0);
            shutdown(peerSocket, SHUT_RD);
            close(peerSocket);
            if (!LISTEN_LOOPED) {
                break;
            }
        }
        close(localSocket);
    }
    return 0;
}

ssize_t SendFile(int socket, FILE *stream, int flags) {
    char buffer[MAXBUF];
    ssize_t bytesSent;
    ssize_t totalSent, totalRead;
    while (fgets(buffer, MAXBUF, stream) != NULL) {
        bytesSent = 0;
        ssize_t nread = strlen(buffer);
        totalRead += nread;
        while ((bytesSent = send(socket, buffer, nread, flags))) {
            totalSent += bytesSent;
            nread -= bytesSent;
            if (nread <= 0)
                break;
            if (bytesSent == -1)
                SysErr("send");
        }
        bzero(buffer, strlen(buffer));
    }
    return totalSent;
}

ssize_t Recv(int socket, int flags) {
    char buffer[MAXBUF];
    ssize_t received = 0, totalReceived = 0;
    while ((received = recv(socket, buffer, MAXBUF, flags))) {
        if (received == -1)
            SysErr("recv");
        fprintf(stdout, "%s", buffer);
        totalReceived += received;
        bzero(buffer, received);
    }
    fflush(stdout); // flush stdout buffer
    return totalReceived;
}
