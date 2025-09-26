#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#include "server.h"

const char IAC_IP[3] = "\xff\xf4";

int handle_read(int fd, request* reqP) {
    /*  Return value:
     *     -1: read failed
     *      0: EOF (client down)
     *      1: read successfully
     */
    int r;
    char buf[BUFFER_SIZE];
    size_t len;

    memset(buf, 0, sizeof(buf));

    // Read in request from client
    r = read(fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    char* p1 = strstr(buf, "\015\012"); // \r\n
    if (p1 == NULL) {
        p1 = strstr(buf, "\012");   // \n
        if (p1 == NULL) {
            if (!strncmp(buf, IAC_IP, 2)) {
                return 0;
            }
        }
    }

    len = min(p1 - buf, BUFFER_SIZE - reqP->length - 1); // with padding
    memmove(reqP->buffer + reqP->length, buf, len);
    reqP->length += len;
    reqP->buffer[reqP->length] = '\0';
    return 1;
}

void reset_request(request * reqP) {
    reqP->length = 0;
    reqP->buffer[0] = '\0';
}

bool opr_read(request req, int new_fd){
    for(int i = 5; i < req.length; i++) {
        if(!isdigit(req.buffer[i])) {
            return false;
        }
    }
    int idx = atoi(req.buffer + 5);
    int note = open("./note.txt", O_RDONLY);
    int index = open("./index", O_RDONLY);
    int sum = 0, paragraph_length = -1, count = 0;
    unsigned char hex;
    while (read(index, &hex, 1) > 0) {
        int value = (int)hex;
        if(count == idx){
            paragraph_length = value;
            break;
        }
        sum += value;
        count++;
    }
    if(idx >= count && paragraph_length == -1){
        return false;
    }
    char content[paragraph_length + 1];
    if(lseek(note, sum, SEEK_SET) != -1){
        read(note, content, paragraph_length);
    }
    write(new_fd, content, paragraph_length);
    return true;
}

bool opr_write(request req, int new_fd){
    int content_start = -1;
    for (int i = 7; i < req.length; i++) {
        if (!isdigit(req.buffer[i])) {
            if (req.buffer[i] == ' ') {
                req.buffer[i] = '\0';
                content_start = i + 1;
                break;
            }
            else {
                return false;
            }
        }
    }
    if (content_start == -1) {
        return false;
    }

    int idx = atoi(req.buffer + 7);
    int note = open("./note.txt", O_RDWR);
    int index = open("./index", O_RDWR);
    int head_length = 0, paragraph_length = -1, paragraph_count = 0;
    unsigned char hex;
    while (read(index, &hex, 1) > 0) {
        int value = (int)hex;
        if (paragraph_count == idx) {
            paragraph_length = value;
            break;
        }
        head_length += value;
        paragraph_count++;
    }
    if (idx >= paragraph_count && paragraph_length == -1) {
        return false;
    }
    int content_length = strlen(req.buffer + content_start);
    if (content_length > MESSAGE_SIZE) {
        content_length = MESSAGE_SIZE;
    }
    char content[content_length + 1];
    memmove(content, req.buffer + content_start, content_length);
    content[content_length] = '\0';

    int file_length = lseek(note, 0, SEEK_END);
    int tail_length = file_length - head_length - paragraph_length;

    char tail[tail_length];
    if (lseek(note, head_length + paragraph_length, SEEK_SET) != -1) {
        read(note, tail, tail_length);
    }
    if (lseek(note, head_length, SEEK_SET) != -1) {
        write(note, content, content_length);
        write(note, tail, tail_length);
    }
    if(paragraph_length > content_length){
        ftruncate(note, head_length + content_length + tail_length);
    }
    unsigned char new_hex = (unsigned char)content_length;
    if(lseek(index, paragraph_count, SEEK_SET) != -1){
        write(index, &new_hex, 1);
    }
    return true;
}

int main(int argc, char** argv) {

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    int listen_fd, new_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);
    unsigned short port = (unsigned short) atoi(argv[1]);
    char buffer[BUFFER_SIZE];

    // Initialize server socket
    listen_fd = init_server(port);
    if (listen_fd < 0) {
        fprintf(stderr, "Failed to initialize server.\n");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);

    // Create one client
    request req;
    reset_request(&req);

    // Blocking accept
    new_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &addrlen);
    if (new_fd == -1) {
        perror("accept");
    }

    while (1) {
        // Blocking read
        int ret = handle_read(new_fd, &req);
        if (ret > 0) {
            printf("read from client: %s\n", req.buffer);
            if(strncmp(req.buffer, "read ", 5) == 0) {
                if(!opr_read(req, new_fd)){
                    write(new_fd, "Invalid command\n", 17);
                }
            }
            else if(strncmp(req.buffer, "update ", 7) == 0) {
                if(!opr_write(req, new_fd)){
                    write(new_fd, "Invalid command\n", 17);
                }
            }
            else if(strncmp(req.buffer, "exit", 5) == 0) {
                close(new_fd);
                break;
            }
            else{
                write(new_fd, "Invalid command\n", 16);
            }
            reset_request(&req);
        } else {
            // Close connection
            printf("client disconnected\n");
            close(new_fd);
            break;
        }
    }

    return 0;
}

// Server
int init_server(unsigned short port) {
    int listen_fd;
    struct sockaddr_in server_addr;

    // Create listening socket
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        return -1;
    }

    // Allow reuse of address
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        close(listen_fd);
        return -1;
    }

    // Bind to port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(listen_fd);
        return -1;
    }

    // Start listening
    if (listen(listen_fd, BACKLOG) == -1) {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}