#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    FILE *fp;

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("Socket creation failed"); exit(1); }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    // Connect to server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed"); exit(1);
    }
    printf("Connected to server\n");

    // Open file to send
    fp = fopen("/home/perpetual/Documents/NP_Project/send_file.txt", "rb");
    if (!fp) {
        perror("File open failed");
        // Send error message to server
        char *error_msg = "ERROR: File not found";
        if (send(sock, error_msg, strlen(error_msg), 0) < 0) {
            perror("Send error message failed");
        }
        close(sock);
        exit(1);
    }

    // Send file data
    int bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        if (send(sock, buffer, bytes_read, 0) < 0) {
            perror("Send failed");
            fclose(fp);
            close(sock);
            exit(1);
        }
    }
    printf("File sent\n");

    fclose(fp);
    close(sock);
    return 0;
}
