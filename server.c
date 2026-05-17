// server.c - Compile with: gcc server.c -o server -lpthread
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define PUBLIC_DIR "public"
#define STORAGE_DIR "storage"
#define TASKS_FILE "storage/tasks.json"

void *handle_client(void *arg);
void serve_file(int client_fd, const char *path);
void handle_api(int client_fd, const char *method, const char *path, const char *body);

int main() {
    mkdir(STORAGE_DIR, 0755);
    
    // Create default tasks.json if not exists
    FILE *f = fopen(TASKS_FILE, "r");
    if (!f) {
        f = fopen(TASKS_FILE, "w");
        fprintf(f, "[]");
        fclose(f);
    } else fclose(f);

    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 10);

    printf("🚀 Todo List server running at http://localhost:%d\n", PORT);
    printf("Open your browser and go to that URL.\n");

    while (1) {
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, NULL, NULL);
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_fd);
        pthread_detach(thread);
    }

    return 0;
}

void *handle_client(void *arg) {
    int client_fd = *(int*)arg;
    free(arg);

    char buffer[BUFFER_SIZE] = {0};
    read(client_fd, buffer, BUFFER_SIZE - 1);

    char method[16], path[256], *body = NULL;
    sscanf(buffer, "%s %s", method, path);

    // Find body (after double \r\n)
    char *body_start = strstr(buffer, "\r\n\r\n");
    if (body_start) body = body_start + 4;

    if (strncmp(path, "/api/", 5) == 0) {
        handle_api(client_fd, method, path, body);
    } else {
        if (strcmp(path, "/") == 0) strcpy(path, "/index.html");
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s%s", PUBLIC_DIR, path);
        serve_file(client_fd, filepath);
    }

    close(client_fd);
    return NULL;
}

void serve_file(int client_fd, const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        const char *notfound = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
        write(client_fd, notfound, strlen(notfound));
        return;
    }

    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *content = malloc(fsize);
    fread(content, 1, fsize, file);
    fclose(file);

    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n\r\n",
             strstr(path, ".css") ? "text/css" : "text/html",
             fsize);

    write(client_fd, header, strlen(header));
    write(client_fd, content, fsize);
    free(content);
}

void handle_api(int client_fd, const char *method, const char *path, const char *body) {
    char response[BUFFER_SIZE];
    const char *json_header = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, DELETE\r\n\r\n";

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/tasks") == 0) {
        FILE *f = fopen(TASKS_FILE, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long len = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *data = malloc(len + 1);
            fread(data, 1, len, f);
            data[len] = '\0';
            fclose(f);

            snprintf(response, sizeof(response), "%s%s", json_header, data);
            free(data);
        } else {
            snprintf(response, sizeof(response), "%s[]", json_header);
        }
    }
    else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/tasks") == 0) {
        FILE *f = fopen(TASKS_FILE, "w");
        if (f && body) {
            fprintf(f, "%s", body);
            fclose(f);
        }
        snprintf(response, sizeof(response), "%s{\"status\":\"ok\"}", json_header);
    }
    else if (strcmp(method, "DELETE") == 0 && strncmp(path, "/api/tasks/", 11) == 0) {
        // For simplicity we just rewrite the whole list on POST from frontend
        snprintf(response, sizeof(response), "%s{\"status\":\"ok\"}", json_header);
    }
    else {
        snprintf(response, sizeof(response), "HTTP/1.1 405 Method Not Allowed\r\n\r\n");
    }

    write(client_fd, response, strlen(response));
}
