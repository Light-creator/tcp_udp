#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8000
#define BUFFER_SIZE 1024

int main() {
    int sock;
    struct sockaddr_in server_addr;
    fd_set read_fds;
    char buffer[BUFFER_SIZE];
    int max_fd;

    // Create UDP socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/Address not supported");
        exit(EXIT_FAILURE);
    }

    printf("UDP client is ready to send messages to %s:%d\n", SERVER_IP, SERVER_PORT);

    while (1) {
        // Clear the buffer and read input from the user
        printf("Enter a message to send (or 'exit' to quit): ");
        fgets(buffer, BUFFER_SIZE, stdin);

        // Remove newline character from input
        buffer[strcspn(buffer, "\n")] = '\0';

        // Exit if the user types 'exit'
        if (strcmp(buffer, "exit") == 0) {
            break;
        }

        // Send the message to the server
        if (sendto(sock, buffer, strlen(buffer), 0,
                   (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("sendto failed");
            continue;
        }

        // Wait for a response from the server using select
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        max_fd = sock;

        // Set a timeout for select (e.g., 5 seconds)
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0) {
            perror("Select error");
            continue;
        } else if (activity == 0) {
            printf("No response from server (timeout).\n");
            continue;
        }

        // If the socket is ready, receive the response
        if (FD_ISSET(sock, &read_fds)) {
            struct sockaddr_in server_response_addr;
            socklen_t server_response_len = sizeof(server_response_addr);
            ssize_t recv_len = recvfrom(sock, buffer, BUFFER_SIZE, 0,
                                        (struct sockaddr *)&server_response_addr, &server_response_len);
            if (recv_len < 0) {
                perror("recvfrom failed");
            } else {
                buffer[recv_len] = '\0';
                printf("Received from server: %s\n", buffer);
            }
        }
    }

    // Close the socket
    close(sock);

    printf("Client exiting...\n");
    return 0;
}
