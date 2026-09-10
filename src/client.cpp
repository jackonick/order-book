#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main() {
    // 1. Create the socket (same as server)
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); exit(1); }

    // 2. Describe WHO we're sending to (the server's address/port)
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9000);                       // must match server's port
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");     // localhost; the server's IP

    // 3. Send a datagram (YOUR logic: what to send)
    const char* msg = "hello";

    ssize_t n = sendto(sockfd, msg, strlen(msg), 0,
        (struct sockaddr*)&server_addr, sizeof(server_addr));

    if (n < 0) { perror("sendto"); exit(1); }

    close(sockfd);
    return 0;
}