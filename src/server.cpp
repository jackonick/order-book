#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>       // close()
#include <arpa/inet.h>    // sockaddr_in, htons, inet_addr
#include <sys/socket.h>

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); exit(1); }

    // port descript
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;               // IPv4
    server_addr.sin_port = htons(9000);             // port 9000, htons = host-to-network byte order
    server_addr.sin_addr.s_addr = INADDR_ANY;       // accept on any local interface

    // bind sock to that port
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    // recv dgram (logic area)
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[1024];

    // recvfrom blocks until a datagram arrives. Returns bytes received.
    // It also fills client_addr with WHO sent it (so you could reply).
    ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
        (struct sockaddr*)&client_addr, &client_len);
    if (n < 0) { perror("recvfrom"); exit(1); }

    // ... do something with the n bytes in buffer ...

    close(sockfd);
    return 0;
}