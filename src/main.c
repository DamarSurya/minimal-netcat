#include <netinet/in.h>
#include <arpa/inet.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>


#define SENDING_MODE 0 // sending mode
#define LISTEN_MODE 1 // listen mode

#define REDIR_STDIN 0 // redirected stdin
#define REDIR_STDOUT 0 // reedirected stdout

#define MAX_BACKLOG 10 // max backlog
#define MAXBUF 4096 // max in buffer size/stored buffer




void WriteError(const char* buffer) {
    write(STDERR_FILENO, buffer, strlen(buffer));
}


int main(int argc, char** argv) {
    if (argc <= 1) {
        WriteError("\t[ERROR]: Not enugh args\n\tArgs:\n\tSending Mode: [ip port buffer] [buffer = str or < file]\n\tListen Mode: -l [ip port]\n");
        return 1;
    }
    
    int MODE = SENDING_MODE;
    char *address;
    int port;
    int af = AF_INET;
    int SOCK_TYPE = SOCK_STREAM;

    int opt, optCounter = 0;
    char shortOpt[1] = {'l'};
    while((opt = getopt(argc, argv, shortOpt)) != -1) {
        ++optCounter;
        switch (opt) {
            case 'l': 
                if (argc-1 < optCounter+2) {
                    char* errBuf = "\t[Error]: Not enough args\n\t-l: listen mode\t\t[-l ip port]\n";
                    WriteError(errBuf);
                    exit(1);
                }
                address = argv[optCounter+1];
                port = atoi(argv[optCounter+2]);
                MODE = LISTEN_MODE;
                break;
            default:
                break;
        }
    }
    if (MODE == SENDING_MODE) {
        if (argc < 3) {
            WriteError("\t[Error]: Not enough args\n\tSending Mode: [ip port]\n");
            return 1;
        }
        char* defaultMessage = "Hola listener\n";
        address = argv[1];
        port = atoi(argv[2]);
        int fd;
        struct in_addr addr;
        struct sockaddr_in sockAddr;
    
        if (inet_pton(af, address, &addr) == -1) {
            perror("inet_pton");
            exit(-1);
        }
            
        if ((fd = socket(af, SOCK_TYPE, 0)) == -1) {
            perror("socket");
            exit(-1);
        }
        sockAddr.sin_addr = addr;
        sockAddr.sin_family = af;
        sockAddr.sin_port = htons(port);
        
        
        if (connect(fd, (struct sockaddr*)&sockAddr, sizeof(sockAddr)) == -1) {
            perror("connect");
            exit(-1);
        }

        char *buffer = NULL;
        
        if (isatty(STDIN_FILENO) == REDIR_STDIN) {
            size_t allocatedSize = 0;
            size_t readSize;

            while((readSize = getline(&buffer, &allocatedSize, stdin)) != -1)  {
                if (send(fd, buffer, readSize, 0) == -1) {
                    perror("send");
                    exit(-1);
                }
            }
            shutdown(fd, SHUT_WR);
        } else if (argc >= 4) {
            buffer = argv[3];    
        } else {
            buffer = defaultMessage;
        }
        
        if (send(fd, buffer, strlen(buffer), 0) == -1) {
            perror("send");
            exit(-1);
        }
        shutdown(fd, SHUT_WR);
        close(fd);
    } else if(MODE == LISTEN_MODE) {
        int localFd, peerFd;
        struct in_addr localAddr, peerAddr;
        struct sockaddr_in localSockAddr, peerSockAddr;
        socklen_t localSockSize, peerSockSize;

        if ((localFd = socket(af, SOCK_STREAM, 0)) == -1) {
            perror("socket");
            exit(-1);
        }
        localSockAddr.sin_addr = localAddr;
        localSockAddr.sin_family = af;
        localSockAddr.sin_port = htons(port);
        localSockSize = sizeof(localSockAddr);

        if (bind(localFd, (struct sockaddr*)&localSockAddr, localSockSize) == -1) {
            perror("bind");
            exit(-1);
        }
        if (listen(localFd, MAX_BACKLOG) == -1) {
            perror("listen");
            exit(-1);
        }
        if ((peerFd = accept(localFd, (struct sockaddr*)&peerSockAddr, &peerSockSize)) == -1) {
            perror("accept");
            exit(-1);
        }
        peerAddr = peerSockAddr.sin_addr;

        char buffer[MAXBUF];
        size_t receive = 0;
        size_t totalReceive = 0;
        while ((receive = recv(peerFd, buffer, MAXBUF, 0)) > 0) {
            totalReceive += receive;
            if (isatty(STDOUT_FILENO) == REDIR_STDOUT) {
                write(STDOUT_FILENO, buffer, receive);
            } else {
                printf("%s", buffer);
            }

            bzero(buffer, receive);
        }

        shutdown(localFd, SHUT_RD);
        close(peerFd);
        close(localFd);
        
    }
    return 0;
}
