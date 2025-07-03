#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    FILE *fp;
    size_t total_bytes = 0;
    int error_received = 0;

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("Socket creation failed"); exit(1); }

    // Set socket options
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind and listen
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed"); exit(1);
    }
    if (listen(server_fd, 1) < 0) { perror("Listen failed"); exit(1); }
    printf("Server listening on port %d...\n", PORT);

    // Accept client
    int addrlen = sizeof(client_addr);
    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
    if (client_fd < 0) { perror("Accept failed"); exit(1); }
    printf("Client connected\n");

    // Open file to save received data
    fp = fopen("received_file.txt", "wb");
    if (!fp) { perror("File open failed"); exit(1); }

    // Receive data
    int bytes_received;
    while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        total_bytes += bytes_received;
        // Check for error message
        if (total_bytes <= strlen("ERROR: File not found") && strncmp(buffer, "ERROR:", 6) == 0) {
            error_received = 1;
            break; // Stop receiving if error message is detected
        }
        fwrite(buffer, 1, bytes_received, fp);
    }
    if (bytes_received < 0) {
        perror("Receive failed");
        error_received = 1;
    }

    fclose(fp);
    close(client_fd);
    close(server_fd);

    // Report outcome
    if (error_received || total_bytes == 0) {
        printf("File transfer failed: %s\n", error_received ? "Client reported error" : "No data received");
        remove("received_file.txt"); // Remove empty or error file
    } else {
        printf("File received and saved (%zu bytes)\n", total_bytes);
    }

    return 0;
}
