#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>

#define EVENT_SIZE  (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))
#define MAX_CLIENTS 10
#define MAX_WATCHES 1024

// Structure to store client info
typedef struct {
    int socket;
    struct sockaddr_in address;
    char *ignore_list;
} Client;

typedef struct {
    int wd;
    char path[PATH_MAX];
} WatchDescriptorMap;

WatchDescriptorMap watch_map[MAX_WATCHES];
int watch_count = 0;


Client clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
int fd;
int wd[MAX_WATCHES];

void *handle_client(void *arg);
void *watch_directory(void *arg);
void add_watch_recursive(const char *dir_path);
void send_file(int client_socket, const char *filepath, const char *relative_path);
bool is_ignored(Client *client, const char *filename);
void send_directory_structure(int client_socket, const char *base_dir, const char *relative_path) ;

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <sync_dir> <port> <max_clients>\n", argv[0]);
        exit(1);
    }
    
    char *sync_dir = argv[1];
    int port = atoi(argv[2]);
    int max_clients = atoi(argv[3]);

    int server_fd, new_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }

    if (listen(server_fd, max_clients) < 0) {
        perror("Listen failed");
        exit(1);
    }

    printf("Server listening on port %d, watching directory: %s\n", port, sync_dir);

    pthread_t watch_thread;
    pthread_create(&watch_thread, NULL, watch_directory, sync_dir);
    
    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (new_socket < 0) {
            perror("Accept failed");
            continue;
        }

        pthread_mutex_lock(&client_mutex);
        if (client_count >= max_clients) {
            printf("Max clients reached, rejecting connection\n");
            close(new_socket);
        } else {
            // Check for free client slot
            for(int i=0;i<MAX_CLIENTS;i++){
                if(clients[i].socket == -1 || clients[i].socket == 0){
                    clients[i].socket = new_socket;
                    clients[i].address = client_addr;
                    pthread_t client_thread;
                    pthread_create(&client_thread, NULL, handle_client, &clients[i]);
                    break;
                }
            }
            client_count++;
        }
        pthread_mutex_unlock(&client_mutex);
    }
    
    close(server_fd);
    return 0;
}

void *handle_client(void *arg) {
    Client *client = (Client *)arg;
    char buffer[256];

    int bytes_received = recv(client->socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        perror("Failed to receive ignore list");
        close(client->socket);
        client->socket = -1;
        return NULL;
    }
    client->ignore_list = (char *)malloc(bytes_received + 1);
    if (!client->ignore_list) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    buffer[bytes_received] = '\0';
    strncpy(client->ignore_list, buffer, bytes_received + 1);
    printf("Client connected with ignore list: %s\n", client->ignore_list);

    // pthread_mutex_lock(&client_mutex);
    // send_directory_structure(client->socket, watch_map[0].path, "");
    // pthread_mutex_unlock(&client_mutex);

    while (1) {
        sleep(1);
        char check_connection[10];
        int bytes_received = recv(client->socket, check_connection, sizeof(check_connection) - 1, MSG_DONTWAIT);
        if (bytes_received == 0) {
            // Print IP address of disconnected client
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client->address.sin_addr, ip, INET_ADDRSTRLEN);
            printf("Client disconnected: %s\n", ip);
            close(client->socket);
            //Free memory allocated for ignore list
            free(client->ignore_list);
            client->socket = -1;
            client_count--;
            break;
        }
    }
    return NULL;
}

void send_directory_structure(int client_socket, const char *base_dir, const char *relative_path) {
    DIR *dir = opendir(base_dir);
    if (!dir) {
        perror("opendir failed");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", base_dir, entry->d_name);

        char new_relative_path[PATH_MAX];
        snprintf(new_relative_path, sizeof(new_relative_path), "%s/%s", relative_path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            // Send directory creation message
            char message[512];
            snprintf(message, sizeof(message), "DIR_CREATE %s\n", new_relative_path);
            send(client_socket, message, strlen(message), 0);

            // Recursively process subdirectories
            send_directory_structure(client_socket, full_path, new_relative_path);
        } else if (entry->d_type == DT_REG) {
            // Send file metadata and content and check if its ignored
            if (is_ignored(clients, entry->d_name)) {
                continue;
            }
            send_file(client_socket, full_path, new_relative_path);
        }
    }
    closedir(dir);
}


bool is_ignored(Client *client, const char *filename) {
    //Check if the extension is in the ignore list
    char *ext = strrchr(filename, '.');
    if (ext) {
        ext++;
        if (strstr(client->ignore_list, ext) != NULL) {
            return true;
        }
    }
    return false;
}

void *watch_directory(void *arg) {
    char *sync_dir = (char *)arg;
    fd = inotify_init();
    if (fd < 0) {
        perror("inotify_init failed");
        return NULL;
    }

    add_watch_recursive(sync_dir);

    char buffer[EVENT_BUF_LEN];
    while (1) {
        int length = read(fd, buffer, EVENT_BUF_LEN);
        if (length < 0) {
            perror("read failed");
            continue;
        }

        int i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            if (event->len) {
                char watched_path[PATH_MAX] = {0};

                for (int j = 0; j < watch_count; j++) {
                    if (watch_map[j].wd == event->wd) {
                        strncpy(watched_path, watch_map[j].path, PATH_MAX);
                        break;
                    }
                }

                if (watched_path[0] == '\0') {
                    fprintf(stderr, "Could not find watched path for wd: %d\n", event->wd);
                    continue;
                }

                char event_path[PATH_MAX];
                snprintf(event_path, sizeof(event_path), "%s/%s", watched_path, event->name);

                // Compute relative path
                const char *relative_path = event_path + strlen(sync_dir) + 1;

                char message[512];
                if(event->mask & IN_CREATE){
                    if(event->mask & IN_ISDIR){
                        add_watch_recursive(event_path);
                        snprintf(message, sizeof(message), "DIR_CREATE %s\n", relative_path);
                        printf("DIR_CREATE %s\n", relative_path);
                    } else {
                        printf("CREATE %s\n", relative_path);
                    }
                }  else if (event->mask & IN_DELETE) {
                    if (event->mask & IN_ISDIR) {
                        snprintf(message, sizeof(message), "DIR_DELETE %s\n", relative_path);
                        printf("DIR_DELETE %s\n", relative_path);
                    } else {
                        snprintf(message, sizeof(message), "DELETE %s\n", relative_path);
                        printf("DELETE %s\n", relative_path);
                    }
                } else if (event->mask & IN_MOVED_FROM) {
                    snprintf(message, sizeof(message), "MOVED_FROM %s\n", relative_path);
                    printf("MOVED_FROM %s\n", relative_path);
                } else if (event->mask & IN_MOVED_TO) {
                    snprintf(message, sizeof(message), "MOVED_TO %s\n", relative_path);
                    printf("MOVED_TO %s\n", relative_path);
                }

                pthread_mutex_lock(&client_mutex);
                for (int j = 0; j < client_count; j++) {
                    if (event->mask & IN_CREATE) {    
                        if (event->mask & IN_ISDIR) {
                            send(clients[j].socket, message, strlen(message), 0);
                        } else {
                            //Check if the file is in the ignore list
                            if (is_ignored(&clients[j], event->name)) {
                                continue;
                            }
                            send_file(clients[j].socket, event_path, relative_path);
                        }
                    } else if (event->mask & IN_DELETE) {
                        if(is_ignored(&clients[j], event->name)){
                            continue;
                        }
                        send(clients[j].socket, message, strlen(message), 0);
                    } else if (event->mask & IN_MOVED_FROM) {
                        //Check if the file is in the ignore list
                        if (is_ignored(&clients[j], event->name)) {
                            continue;
                        }
                        send(clients[j].socket, message, strlen(message), 0);
                    } else if (event->mask & IN_MOVED_TO) {
                        //Check if the file is in the ignore list
                        if (is_ignored(&clients[j], event->name)) {
                            continue;
                        }
                        send(clients[j].socket, message, strlen(message), 0);
                    } 
                }
                pthread_mutex_unlock(&client_mutex);
            }
            i += EVENT_SIZE + event->len;
        }
    }
    return NULL;
}

void send_file(int client_socket, const char *filepath, const char *relative_path) {
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        perror("Failed to open file");
        return;
    }

    // Send CREATE command
    char message[512];
    // snprintf(message, sizeof(message), "CREATE %s\n", relative_path);
    // send(client_socket, message, strlen(message), 0);
    //format: FILE <relative_path> <file_size>
    

    // Send file size
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    printf("Sending file %s, size: %ld bytes\n", relative_path, filesize);
    rewind(fp);
    snprintf(message, sizeof(message), "FILE %s %ld", relative_path, filesize);
    send(client_socket, message, strlen(message), 0);

    // Send file content
    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }

    fclose(fp);
}




void add_watch_recursive(const char *dir_path) {
    int watch_descriptor = inotify_add_watch(fd, dir_path, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
    if (watch_descriptor < 0) {
        perror("inotify_add_watch failed");
        return;
    }

    // Store the watch descriptor mapping
    if (watch_count < MAX_WATCHES) {
        watch_map[watch_count].wd = watch_descriptor;
        strncpy(watch_map[watch_count].path, dir_path, PATH_MAX);
        watch_count++;
    }

    // Recursively add watches to subdirectories
    DIR *dir = opendir(dir_path);
    if (!dir) {
        perror("opendir failed");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char sub_dir_path[PATH_MAX];
            snprintf(sub_dir_path, sizeof(sub_dir_path), "%s/%s", dir_path, entry->d_name);
            add_watch_recursive(sub_dir_path);
        }
    }
    closedir(dir);
}
