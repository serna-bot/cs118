#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/socket.h>

/**
 * Project 1 starter code
 * All parts needed to be changed/added are marked with TODO
 */

#define BUFFER_SIZE 1024
#define DEFAULT_SERVER_PORT 8081
#define DEFAULT_REMOTE_HOST "131.179.176.34"
#define DEFAULT_REMOTE_PORT 5001

struct server_app {
    // Parameters of the server
    // Local port of HTTP server
    uint16_t server_port;

    // Remote host and port of remote proxy
    char *remote_host;
    uint16_t remote_port;
};

// The following function is implemented for you and doesn't need
// to be change
void parse_args(int argc, char *argv[], struct server_app *app);

// The following functions need to be updated
void handle_request(struct server_app *app, int client_socket);
void serve_local_file(int client_socket, const char *path);
void proxy_remote_file(struct server_app *app, int client_socket, const char *path);

// The main function is provided and no change is needed
int main(int argc, char *argv[])
{
    struct server_app app;
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    int ret;

    parse_args(argc, argv, &app);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(app.server_port);

    // The following allows the program to immediately bind to the port in case
    // previous run exits recently
    int optval = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) == -1) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", app.server_port);

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket == -1) {
            perror("accept failed");
            continue;
        }
        
        printf("Accepted connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        handle_request(&app, client_socket);
        close(client_socket);
    }

    close(server_socket);
    return 0;
}

void parse_args(int argc, char *argv[], struct server_app *app)
{
    int opt;

    app->server_port = DEFAULT_SERVER_PORT;
    app->remote_host = NULL;
    app->remote_port = DEFAULT_REMOTE_PORT;

    while ((opt = getopt(argc, argv, "b:r:p:")) != -1) {
        switch (opt) {
        case 'b':
            app->server_port = atoi(optarg);
            break;
        case 'r':
            app->remote_host = strdup(optarg);
            break;
        case 'p':
            app->remote_port = atoi(optarg);
            break;
        default: /* Unrecognized parameter or "-?" */
            fprintf(stderr, "Usage: server [-b local_port] [-r remote_host] [-p remote_port]\n");
            exit(-1);
            break;
        }
    }

    if (app->remote_host == NULL) {
        app->remote_host = strdup(DEFAULT_REMOTE_HOST);
    }
}

void handle_request(struct server_app *app, int client_socket) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // Read the request from HTTP client
    // Note: This code is not ideal in the real world because it
    // assumes that the request header is small enough and can be read
    // once as a whole.
    // However, the current version suffices for our testing.
    bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        return;  // Connection closed or error
    }

    buffer[bytes_read] = '\0';
    // copy buffer to a new string
    char *request = malloc(strlen(buffer) + 1);
    strcpy(request, buffer);

    // TODO: Parse the header and extract essential fields, e.g. file name
    // Hint: if the requested path is "/" (root), default to index.html

    char unformatted_file_name[500];
    char file_name[500];
    // create var for requested filename
    char *format = "GET %500s%n HTTP";
    int file_name_size;
    // printf("Request %s\n", request);
    sscanf(request, format, unformatted_file_name, &file_name_size);
    unformatted_file_name[file_name_size] = '\0';

    int i = 0;
    
    while (i <= file_name_size) {
        for (int j = 0; j <=  file_name_size; j++) {
            if (unformatted_file_name[i] == '%' && i + 2 <= file_name_size && unformatted_file_name[i + 1] == '2') {
                if (unformatted_file_name[i + 2] == '0')
                    file_name[j] = ' ';
                else if (unformatted_file_name[i + 2] == '5')
                    file_name[j] = '%';
                i += 3;

            }
            else {
                file_name[j] = unformatted_file_name[i];
                i++;
            }
        }
    }
    // printf("Filename %s\n", file_name);

    if (strcmp(file_name, "/") == 0) {
        strcpy(file_name, "/index.html"); // handling root case
    }
    // printf("Filename %s\n", file_name);
    char *tmp = strrchr(file_name, '.');
    char *file_type = (char*)malloc((50) * sizeof(char));

    if (strrchr(file_name, '.') != NULL) {
        strcpy(file_type, tmp);
    }
    else {
        strcpy(file_type, "");
        // printf("filetype: %s", file_type);
    }
    // printf("filetype: %s", file_type);
    // TODO: Implement proxy and call the function under condition
    // specified in the spec
    if (strcmp(file_type, ".ts") == 0) {
        proxy_remote_file(app, client_socket, request);
    } 
    else {
        serve_local_file(client_socket, file_name);
    }
}

void serve_local_file(int client_socket, const char *path) {
    // TODO: Properly implement serving of local files
    // The following code returns a dummy response for all requests
    // but it should give you a rough idea about what a proper response looks like
    // What you need to do 
    // (when the requested file exists):
    // * Open the requested file
    // * Build proper response headers (see details in the spec), and send them
    // * Also send file content
    // (When the requested file does not exist):
    // * Generate a correct response

    char filepath[strlen(path) + 1];
    strcpy(filepath, ".");
    strcat(filepath, path);
    char *tmp = strrchr(path, '.');
    char *file_type = (char*)malloc((50) * sizeof(char));
    
    if (strrchr(path, '.') != NULL) {
        strcpy(file_type, tmp);
    }
    else {
        strcpy(file_type, "");
        // printf("filetype: %s", file_type);
    }
    // printf("filetype: %s", file_type);
    FILE* fp = fopen(filepath, "rb");

    
    if(fp) {
        fseek(fp, 0, SEEK_END); // seek to end of file
        size_t file_size = ftell(fp); // get current file pointer
        fseek(fp, 0, SEEK_SET);
        // fread(file_content, sizeof(unsigned char), file_size, fp);
        // char response[1000085];
        char* response_ptr = (char*)malloc((BUFFER_SIZE) * sizeof(char));
        char* content_type = (char*)malloc((50) * sizeof(char));
        strcpy(content_type, "text/plain");
        if (strcmp(file_type, "") == 0) {
            memset(content_type, 0, (50) * sizeof(char));
            strcpy(content_type, "application/octet-stream");
            unsigned char* file_content_ptr = (unsigned char*)malloc(file_size * sizeof(unsigned char));
            fread(file_content_ptr, 1, file_size, fp);
            sprintf(response_ptr, "HTTP/1.0 200 OK\r\nContent-Type: %s; charset=UTF-8\r\nContent-Length: %ld\r\n\r\n", content_type, file_size);
            send(client_socket, response_ptr, strlen(response_ptr), 0);
            send(client_socket, file_content_ptr, file_size, 0);

            free(file_content_ptr);
            free(content_type);
            free(response_ptr);
        }
        else if (strcmp(file_type, ".jpg") == 0) {
            memset(content_type, 0, (50) * sizeof(char));
            strcpy(content_type, "image/jpeg");
            unsigned char* file_content_ptr = (unsigned char*)malloc(file_size * sizeof(unsigned char));
            fread(file_content_ptr, 1, file_size, fp);
            sprintf(response_ptr, "HTTP/1.0 200 OK\r\nContent-Type: %s; charset=UTF-8\r\nContent-Length: %ld\r\n\r\n", content_type, file_size);
            send(client_socket, response_ptr, strlen(response_ptr), 0);
            send(client_socket, file_content_ptr, file_size, 0);

            free(file_content_ptr);
            free(content_type);
            free(response_ptr);
        }
        else {
            if (strcmp(file_type, ".html") == 0) {
                memset(content_type, 0, (50) * sizeof(char));
                strcpy(content_type, "text/html");
            }
            char* file_content_ptr = (char*)malloc(file_size * sizeof(char));
            fread(file_content_ptr, 1, file_size, fp);
            sprintf(response_ptr, "HTTP/1.0 200 OK\r\nContent-Type: %s; charset=UTF-8\r\nContent-Length: %ld\r\n\r\n", content_type, file_size);
            send(client_socket, response_ptr, strlen(response_ptr), 0);
            send(client_socket, file_content_ptr, file_size, 0);

            free(file_content_ptr);
            free(content_type);
            free(response_ptr);
        }
    }
    else {
        char response[] = "HTTP/1.0 404 Not Found\r\n"
                      "Content-Type: text/plain; charset=UTF-8\r\n"
                      "Content-Length: 15\r\n"
                      "\r\n"
                      "File not found.";

        send(client_socket, response, strlen(response), 0);
    }
    fclose(fp);
}

void proxy_remote_file(struct server_app *app, int client_socket, const char *request) {
    // TODO: Implement proxy request and replace the following code
    // What's needed:
    // * Connect to remote server (app->remote_server/app->remote_port)
    // * Forward the original request to the remote server
    // * Pass the response from remote server back
    // Bonus:
    // * When connection to the remote server fail, properly generate
    // HTTP 502 "Bad Gateway" response

    struct sockaddr_in remote_addr;
    int status;

    int remote_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (remote_socket == -1) {
        perror("remote socket failed");
        exit(EXIT_FAILURE);
    }

    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = INADDR_ANY;
    remote_addr.sin_port = htons(app->remote_port);

    // printf("Server listening on port %d\n", app->remote_port);

    if (inet_pton(AF_INET, app->remote_host, &remote_addr.sin_addr) == -1) { 
        perror("Invalid address");
        char response[] = "HTTP/1.0 404 Not Found\r\n\r\nNot Found";
        send(client_socket, response, strlen(response), 0);
        close(remote_socket);
        return; 
    } 

    status = connect(remote_socket, (struct sockaddr*)&remote_addr, sizeof(remote_addr));
    if (status <= -1) {
        perror("Connection failed");
        char response[] = "HTTP/1.0 502 Bad Gateway\r\n\r\nBad Gateway";
        send(client_socket, response, strlen(response), 0);
        close(remote_socket);
        return;
    }


    // unsigned char *remote_response = (unsigned char*)malloc((BUFFER_SIZE) * sizeof(unsigned char));
    // ssize_t response_bytes, response_size = 0;
    // send(remote_socket, request, strlen(request), 0); 

    // while ((response_bytes = recv(remote_socket, remote_response + response_size, (BUFFER_SIZE) * sizeof(unsigned char), 0)) > 0) {
    //     if (response_bytes <= -1 ) {
    //         perror("Socket Read Error");
    //         return;
    //     }
    //     else {
    //         response_size += response_bytes;
    //         unsigned char *new_remote_response = (unsigned char *) realloc(remote_response, (BUFFER_SIZE) * sizeof(unsigned char));
    //         if (new_remote_response == NULL) {
    //             perror("Realloc Error");
    //             break;
    //         }
    //         remote_response = new_remote_response;
    //     }
    // }

    // close(remote_socket);


    // send(client_socket, remote_response, response_size, 0);
    // free(remote_response);
    
    unsigned char *remote_response = (unsigned char*)malloc((BUFFER_SIZE) * sizeof(unsigned char));
    ssize_t response_bytes = 0;
    send(remote_socket, request, strlen(request), 0); 

    while ((response_bytes = recv(remote_socket, remote_response, (BUFFER_SIZE) * sizeof(unsigned char), 0)) > 0) {
        if (response_bytes <= -1 ) {
            perror("Socket Read Error");
            return;
        }
        else {
            send(client_socket, remote_response, response_bytes, 0);
            memset(remote_response, 0, (BUFFER_SIZE) * sizeof(unsigned char));
        }
    }

    close(remote_socket);
    free(remote_response);
}