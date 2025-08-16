#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>


#define BUFFER_SIZE 1024

void sync_files(int sock, const char *sync_dir);
void send_ignore_list(int sock);
void receive_file(int sock, const char *sync_dir, const char *relative_path, size_t file_size);

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <server_ip> <port> <sync_directory>\n", argv[0]);
        exit(1);
    }

    char *server_ip = argv[1];
    int port = atoi(argv[2]);
    char *sync_dir = argv[3];

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address / Address not supported");
        exit(1);
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        exit(1);
    }

    printf("Connected to server at %s:%d\n", server_ip, port);

    // Send ignore list to server
    send_ignore_list(sock);

    // Start syncing files
    sync_files(sock, sync_dir);

    close(sock);
    return 0;
}

void send_ignore_list(int sock) {
    // char ignore_list[] = "c, pdf";  // Modify as needed
    // send(sock, ignore_list, strlen(ignore_list), 0);
    // printf("Sent ignore list to server: %s\n", ignore_list);
    FILE *file = fopen("ignore_list.txt", "r");
        if (!file) {
            perror("Failed to open ignore_list.txt");
            return;
        } else {
            printf("Opened ignore_list.txt\n");
        }
    
        char ignore_list[256];  // Adjust size as needed
        if (!fgets(ignore_list, sizeof(ignore_list), file)) {
            perror("Failed to read ignore_list.txt");
            fclose(file);
            return;
        }
        fclose(file);
    
        // Remove newline if present
        size_t len = strlen(ignore_list);
        if (len > 0 && (ignore_list[len - 1] == '\n' || ignore_list[len - 1] == '\r')) {
            ignore_list[len - 1] = '\0';
        }
        printf("Read ignore list: %s\n", ignore_list);
        send(sock, ignore_list, strlen(ignore_list), 0);
        printf("Sent ignore list to server: %s\n", ignore_list);
}

void sync_files(int sock, const char *sync_dir) {
    char buffer[BUFFER_SIZE];
    int bytes_received;
    char moved_from_path[PATH_MAX] = "";  // To store the old path temporarily

    while ((bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';

        if (strncmp(buffer, "FILE ", 5) == 0) {
            // Expecting format: FILE <relative_path> <file_size>
            char relative_path[PATH_MAX];
            size_t file_size;
            if (sscanf(buffer + 5, "%s %zu", relative_path, &file_size) == 2) {
                receive_file(sock, sync_dir, relative_path, file_size);
            }
        } else if (strncmp(buffer, "DELETE ", 7) == 0) {
            // Handle DELETE command: DELETE <relative_path>
            char relative_path[PATH_MAX];
            if (sscanf(buffer + 7, "%s", relative_path) == 1) {
                char full_path[PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s/%s", sync_dir, relative_path);
                if (remove(full_path) == 0) {
                    printf("Deleted: %s\n", full_path);
                } else {
                    perror("Failed to delete file");
                }
            }
        }else if (strncmp(buffer, "DIR_CREATE ", 11) == 0) {
            // DIR_CREATE <relative_path>
            char relative_path[PATH_MAX];
            if (sscanf(buffer + 11, "%s", relative_path) == 1) {
                char full_path[PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s/%s", sync_dir, relative_path);
                if (mkdir(full_path, 0777) == 0) {
                    printf("Created directory: %s\n", full_path);
                } else {
                    perror("Failed to create directory");
                }
            }
        } else if (strncmp(buffer, "DIR_DELETE ", 11) == 0) {
            // DIR_DELETE <relative_path>
            char relative_path[PATH_MAX];
            if (sscanf(buffer + 11, "%s", relative_path) == 1) {
                char full_path[PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s/%s", sync_dir, relative_path);
                if (rmdir(full_path) == 0) {
                    printf("Deleted directory: %s\n", full_path);
                } else {
                    perror("Failed to delete directory");
                }
            }
        } else if (strncmp(buffer, "MOVED_FROM ", 11) == 0) {
            // Store the old path temporarily
            sscanf(buffer + 11, "%s", moved_from_path);
            printf("Moved from: %s\n", moved_from_path);
        } else if (strncmp(buffer, "MOVED_TO ", 9) == 0) {
            // Handle MOVE by renaming the stored path to the new path
            char new_relative_path[PATH_MAX];
            printf("File will be moved from %s to %s\n", moved_from_path, buffer + 9);
            if (sscanf(buffer + 9, "%s", new_relative_path) == 1 && moved_from_path[0] != '\0') {
                char old_full_path[PATH_MAX], new_full_path[PATH_MAX];
                snprintf(old_full_path, sizeof(old_full_path), "%s/%s", sync_dir, moved_from_path);
                snprintf(new_full_path, sizeof(new_full_path), "%s/%s", sync_dir, new_relative_path);

                if (rename(old_full_path, new_full_path) == 0) {
                    printf("Moved: %s -> %s\n", old_full_path, new_full_path);
                } else {
                    perror("Failed to move file");
                }
                moved_from_path[0] = '\0';  // Reset the stored path
            }
        } else {
            printf("Server: %s\n", buffer);
        }
    }

    if (bytes_received == 0) {
        printf("Server disconnected.\n");
    } else {
        perror("recv failed");
    }
}

void receive_file(int sock, const char *sync_dir, const char *relative_path, size_t file_size) {
    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", sync_dir, relative_path);

    // Create directories if necessary
    char dir_path[PATH_MAX];
    strncpy(dir_path, full_path, sizeof(dir_path));
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dir_path, 0777); // Create the directory
    }

    // Open file for writing
    int fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        perror("File creation failed");
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytes_received = 0;
    ssize_t len;

    while (bytes_received < file_size && (len = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        write(fd, buffer, len);
        bytes_received += len;
    }

    close(fd);

    if (bytes_received == file_size) {
        printf("Received file: %s (%zu bytes)\n", relative_path, file_size);
    } else {
        printf("File transfer incomplete: %s\n", relative_path);
    }
}
