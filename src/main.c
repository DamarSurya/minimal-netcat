#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>


// debug
#define DEBUG_MODE 1


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

#define REDIR_STDIN 0  // redirected stdin
#define REDIR_STDOUT 0 // reedirected stdout
#define MAX_BACKLOG 1  // max backlog
#define MAXBUF 4096    // max in buffer size/stored buffer

enum {
    SENDING_MODE, // sending mode
    LISTEN_MODE   // listen mode
};

int MODE = SENDING_MODE;
int REPEATER = false;

const int SOCK_TYPE = SOCK_STREAM;
const int DEFAULT_PORT = 3000;
const int DOMAIN = AF_INET;
const int enableREUSEADDR = 1;

bool IP_IS_SET = false;
bool PORT_IS_SET = false;

int LOCAL_PORT;
char *LOCAL_ADDRESS_CHAR;
struct in_addr LOCAL_ADDRESS;

void SendLines(int socket, FILE *stream) {
    char line[MAXBUF];
    ssize_t bytesSent;
    ssize_t totalSent, totalRead;
    while (fgets(line, MAXBUF, stream) != NULL) {
        bytesSent = 0;
        ssize_t read = strlen(line);
        totalRead += read;
        while ((bytesSent = send(socket, line, read, 0))) {
        totalSent += bytesSent;
            read -= bytesSent;
            if (read <= 0)
                break;
            if (bytesSent == -1)
                SysErr("send");
        }
    } 
    if (DEBUG_MODE) printf("Total read: %lu, Total sent: %lu\n", totalRead, totalSent);
}
int CreateSocket(int domain, int type, int proto) {
    int ec;
    if ((ec = socket(domain, type, proto)) == -1)
        SysErr("socket");
    return ec;
}

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
                    REPEATER = true;
                    break;
                case 'i':
                    if (inet_pton(DOMAIN, optarg, &LOCAL_ADDRESS) == -1)
                        SysErr("inet_pton");
                    LOCAL_ADDRESS_CHAR = optarg;
                    IP_IS_SET = true;
                    break;
                case 'p':
                    LOCAL_PORT = atoi(optarg);
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
        LOCAL_ADDRESS_CHAR = argv[1];
        LOCAL_PORT = atoi(argv[2]);
        int fd;
        struct in_addr addr;
        struct sockaddr_in sockAddr;
        fd = CreateSocket(DOMAIN, SOCK_TYPE, 0);

        if (inet_pton(DOMAIN, LOCAL_ADDRESS_CHAR, &addr) == -1)
            SysErr("inet_pton");
        sockAddr.sin_addr = addr;
        sockAddr.sin_family = DOMAIN;
        sockAddr.sin_port = htons(LOCAL_PORT);
        if (connect(fd, (struct sockaddr *)&sockAddr, sizeof(sockAddr)) == -1)
            SysErr("connect");
        if (isatty(STDIN_FILENO) == REDIR_STDIN) {
            SendLines(fd, stdin);
            shutdown(fd, SHUT_WR);
        }
    } else if (MODE == LISTEN_MODE) {
        if (!IP_IS_SET) {
            LOCAL_ADDRESS_CHAR = "0.0.0.0";
            LOCAL_ADDRESS.s_addr = htonl(INADDR_ANY);
        }
        if (!PORT_IS_SET) {
            LOCAL_PORT = DEFAULT_PORT;
        }
        int localFd;
        struct sockaddr_in localSockAddr;
        socklen_t localSockSize;
        localFd = CreateSocket(DOMAIN, SOCK_TYPE, 0);

        localSockAddr.sin_addr = LOCAL_ADDRESS;
        localSockAddr.sin_family = DOMAIN;
        localSockAddr.sin_port = htons(LOCAL_PORT);

        setsockopt(localFd, SOL_SOCKET, SO_REUSEADDR, &enableREUSEADDR,
                   sizeof(enableREUSEADDR));
        localSockSize = sizeof(localSockAddr);

        if (bind(localFd, (struct sockaddr *)&localSockAddr, localSockSize) ==
            -1) {
            SysErr("bind");
        }
        if (listen(localFd, MAX_BACKLOG) == -1) {
            SysErr("listen");
        }
        while (1) {
            int peerFd;
            if ((peerFd = accept(localFd, NULL, NULL)) == -1) {
                SysErr("accept");
            }
            char inbuf[MAXBUF];
            ssize_t receive = 0, totalReceive = 0;

            while ((receive = recv(peerFd, inbuf, MAXBUF, 0))) {
                if (receive == -1)
                    SysErr("recv");
                fprintf(stdout, "%s", inbuf);
                totalReceive += receive;
                bzero(inbuf, receive);
            }
            shutdown(peerFd, SHUT_RD);
            close(peerFd);
            fflush(stdout);

            if (DEBUG_MODE) printf("Total receive: %lu\n", totalReceive);
            if (!REPEATER) {
                break;
            }
            close(peerFd);
        }
        close(localFd);
    }
    return 0;
}
