#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>

#define PORT 8081
#define BUFFER_SIZE 1024
#define MAX_FILENAME 256
#define MAX_FILE_SIZE (10 * 1024 * 1024) // 10 MB limit

// Global variables for signal handling
static volatile int keep_running = 1;
int sock = -1;

void handle_sigint(int sig) {
    keep_running = 0;
    printf("Received SIGINT, shutting down...\n");
    if (sock >= 0) {
        close(sock);
        sock = -1;
    }
    exit(0);
}

int main() {
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE], filename[MAX_FILENAME], response[BUFFER_SIZE];
    char cwd[1024];
    FILE *fp;

    // Print current working directory
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("Client running in directory: %s\n", cwd);
    } else {
        perror("getcwd failed");
    }

    // Set up signal handler
    signal(SIGINT, handle_sigint);

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Set timeout for recv
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("Setsockopt timeout failed");
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    // Connect to server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(1);
    }
    printf("Connected to server\n");

    while (keep_running) {
        // Get filename from user
        printf("Enter filename to send (or 'done' to quit): ");
        if (fgets(filename, MAX_FILENAME, stdin) == NULL) {
            printf("Failed to read filename\n");
            break;
        }
        filename[strcspn(filename, "\n")] = '\0';

        // Check for exit condition
        if (strcmp(filename, "done") == 0) {
            uint32_t len = htonl(4);
            if (send(sock, &len, sizeof(len), 0) < 0 || send(sock, "DONE", 4, 0) < 0) {
                perror("Send DONE failed");
            }
            printf("Client finished sending files\n");
            break;
        }

        // Open file
        fp = fopen(filename, "rb");
        if (!fp) {
            perror("File open failed");
            printf("Attempted to open: %s/%s\n", cwd, filename);
            uint32_t len = htonl(strlen("ERROR: File not found"));
            if (send(sock, &len, sizeof(len), 0) < 0 ||
                send(sock, "ERROR: File not found", strlen("ERROR: File not found"), 0) < 0) {
                printf("Send error message failed\n");
            }
            int bytes_received = recv(sock, response, BUFFER_SIZE - 1, 0);
            if (bytes_received > 0) {
                response[bytes_received] = '\0';
                printf("Server response: %s\n", response);
            } else if (bytes_received < 0) {
                perror("Receive failed");
            }
            continue;
        }

        // Get file size
        fseek(fp, 0, SEEK_END);
        uint32_t file_len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        printf("File size: %u bytes\n", file_len);
        if (file_len > MAX_FILE_SIZE) {
            printf("File too large: %u bytes, limit is %d\n", file_len, MAX_FILE_SIZE);
            fclose(fp);
            continue;
        }

        // Send filename length and filename
        uint32_t filename_len = htonl(strlen(filename));
        if (send(sock, &filename_len, sizeof(filename_len), 0) < 0 ||
            send(sock, filename, strlen(filename), 0) < 0) {
            printf("Send filename failed\n");
            fclose(fp);
            break;
        }
        printf("Sent filename: %s\n", filename);

        // Send file length
        uint32_t file_len_net = htonl(file_len);
        if (send(sock, &file_len_net, sizeof(file_len_net), 0) < 0) {
            printf("Send file length failed\n");
            fclose(fp);
            break;
        }
        printf("Sent file length: %u bytes\n", file_len);

        // Send file data with progress
        size_t total_bytes = 0;
        int bytes_read;
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
            if (send(sock, buffer, bytes_read, 0) < 0) {
                printf("Send failed\n");
                fclose(fp);
                break;
            }
            total_bytes += bytes_read;
            printf("Sent %zu bytes of %s\n", total_bytes, filename);
        }
        fclose(fp);

        // Receive server response
        int bytes_received = recv(sock, response, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            response[bytes_received] = '\0';
            printf("Server response: %s\n", response);
        } else if (bytes_received < 0) {
            perror("Receive failed");
        } else {
            printf("Server closed connection\n");
        }
    }

    if (sock >= 0) {
        close(sock);
        sock = -1;
    }
    printf("Client exiting\n");
    return 0;
}
