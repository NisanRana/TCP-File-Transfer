#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>

#define PORT 8081
#define BUFFER_SIZE 1024
#define MAX_FILENAME 256
#define MAX_FILE_SIZE (10 * 1024 * 1024) // 10 MB limit

// Global variables for signal handling
static volatile int keep_running = 1;
int server_fd = -1, client_fd = -1;
FILE *fp = NULL;

void handle_sigint(int sig) {
    keep_running = 0;
    printf("Received SIGINT, shutting down...\n");
    if (fp) {
        fflush(fp);
        fclose(fp);
        fp = NULL;
    }
    if (client_fd >= 0) {
        close(client_fd);
        client_fd = -1;
    }
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    exit(0);
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE], filename[MAX_FILENAME], response[BUFFER_SIZE];
    char cwd[1024];

    // Print current working directory
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("Server running in directory: %s\n", cwd);
    } else {
        perror("getcwd failed");
    }

    // Set up signal handler
    signal(SIGINT, handle_sigint);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    printf("Socket created\n");

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(server_fd);
        exit(1);
    }
    printf("Socket options set\n");

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(1);
    }
    printf("Socket bound to port %d\n", PORT);

    // Listen
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(1);
    }
    printf("Server listening on port %d...\n", PORT);

    while (keep_running) {
        // Accept client
        int addrlen = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (client_fd < 0) {
            if (keep_running) perror("Accept failed");
            continue;
        }
        printf("Client connected\n");

        // Handle multiple files
        while (keep_running) {
            size_t total_bytes = 0;
            int error_received = 0;

            // Receive filename length
            uint32_t filename_len;
            int bytes_received = recv(client_fd, &filename_len, sizeof(uint32_t), MSG_WAITALL);
            if (bytes_received != sizeof(uint32_t)) {
                printf("Client disconnected or error receiving filename length: %s\n",
                       bytes_received < 0 ? strerror(errno) : "closed");
                break;
            }
            filename_len = ntohl(filename_len);
            printf("Received filename length: %u\n", filename_len);
            if (filename_len >= MAX_FILENAME) {
                printf("Filename too long: %u bytes\n", filename_len);
                send(client_fd, "Transfer failed: Filename too long", 33, 0);
                continue;
            }

            // Receive filename
            bytes_received = recv(client_fd, filename, filename_len, MSG_WAITALL);
            if (bytes_received != filename_len) {
                printf("Error receiving filename: expected %u, got %d\n", filename_len, bytes_received);
                send(client_fd, "Transfer failed: Filename error", 30, 0);
                break;
            }
            filename[bytes_received] = '\0';
            // Sanitize filename
            for (int i = 0; filename[i]; i++) {
                if (filename[i] == '\n' || filename[i] == '/' || filename[i] == '\\') {
                    filename[i] = '_';
                }
            }
            printf("Received filename: %s\n", filename);

            // Check for end of session or error
            if (strcmp(filename, "DONE") == 0) {
                printf("Client finished sending files\n");
                break;
            }
            if (strncmp(filename, "ERROR:", 6) == 0) {
                printf("Client reported error: %s\n", filename);
                if (send(client_fd, "Transfer failed: Client error", 28, 0) < 0) {
                    perror("Send feedback failed");
                }
                continue;
            }

            // Open file
            fp = fopen(filename, "wb");
            if (!fp) {
                perror("File open failed");
                printf("Attempted to save file in: %s/%s\n", cwd, filename);
                send(client_fd, "Transfer failed: Server cannot open file", 39, 0);
                continue;
            }
            printf("Saving file to: %s/%s\n", cwd, filename);

            // Receive file data length
            uint32_t file_len;
            bytes_received = recv(client_fd, &file_len, sizeof(uint32_t), MSG_WAITALL);
            if (bytes_received != sizeof(uint32_t)) {
                printf("Error receiving file length: %s\n", bytes_received < 0 ? strerror(errno) : "closed");
                fclose(fp);
                fp = NULL;
                send(client_fd, "Transfer failed: File length error", 33, 0);
                continue;
            }
            file_len = ntohl(file_len);
            printf("Received file length: %u bytes\n", file_len);
            if (file_len > MAX_FILE_SIZE) {
                printf("File size exceeds limit (%d bytes) for %s\n", MAX_FILE_SIZE, filename);
                fclose(fp);
                fp = NULL;
                send(client_fd, "Transfer failed: File too large", 30, 0);
                remove(filename);
                continue;
            }

            // Receive file data
            total_bytes = 0;
            while (total_bytes < file_len) {
                size_t remaining = file_len - total_bytes;
                size_t to_read = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE;
                bytes_received = recv(client_fd, buffer, to_read, MSG_WAITALL);
                if (bytes_received <= 0) {
                    printf("Receive failed or client disconnected: %s\n",
                           bytes_received < 0 ? strerror(errno) : "closed");
                    error_received = 1;
                    break;
                }
                total_bytes += bytes_received;
                fwrite(buffer, 1, bytes_received, fp);
                fflush(fp);
                printf("Received %d bytes for %s, total: %zu/%u\n", bytes_received, filename, total_bytes, file_len);
            }
            if (total_bytes < file_len) {
                printf("Incomplete file data: received %zu, expected %u\n", total_bytes, file_len);
                error_received = 1;
            }

            fclose(fp);
            fp = NULL;

            // Send feedback
            if (error_received || total_bytes == 0) {
                printf("File transfer failed for %s: %s\n", filename,
                       total_bytes == 0 ? "No data received" : "Incomplete data");
                if (send(client_fd, "Transfer failed: No data received", 32, 0) < 0) {
                    perror("Send feedback failed");
                }
                remove(filename);
            } else {
                printf("File received and saved: %s (%zu bytes)\n", filename, total_bytes);
                snprintf(response, BUFFER_SIZE, "File %s received successfully (%zu bytes)", filename, total_bytes);
                if (send(client_fd, response, strlen(response) + 1, 0) < 0) {
                    perror("Send feedback failed");
                }
            }
        }

        close(client_fd);
        client_fd = -1;
    }

    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    printf("Server exiting\n");
    return 0;
}
