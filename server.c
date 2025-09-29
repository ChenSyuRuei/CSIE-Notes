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

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

const char IAC_IP[3] = "\xff\xf4";

int handle_read(Client* cltP) {
    /*
     * 返回值:
     * -1: 讀取失敗
     *  0: EOF (客戶端已離線)
     *  1: 已讀取一條完整的指令
     *  2: 只讀到部分指令，需要更多資料
     */

    // 計算請求緩衝區剩餘的空間，減 1 是為了保留給 null-terminator ('\0')
    size_t space_left = BUFFER_SIZE - cltP->rlength - 1;

    // 從 socket 讀取資料到一個臨時的緩衝區
    bool command_complete = false;
    int r;
    char buffer[BUFFER_SIZE];    
    size_t len;

    memset(buffer, 0, sizeof(buffer));
    r = read(cltP->fd, buffer, sizeof(buffer));
    if (r < 0) return -1;
    if (r == 0) return 0;
    char* p1 = strstr(buffer, "\015\012"); // \r\n
    if (p1 == NULL) {
        p1 = strstr(buffer, "\012");   // \n
        if (p1 == NULL) {
            if (!strncmp(buffer, IAC_IP, 2)) {
                return 0;
            }
        } else command_complete = true;
    } else command_complete = true;

    // 將新讀取的資料附加到主請求緩衝區
    len = min((p1 == NULL) ? r : (p1 - buffer), space_left);
    memcpy(cltP->rbuffer + cltP->rlength, buffer, len);
    cltP->rlength += len;
    cltP->rbuffer[cltP->rlength] = '\0';

    if (command_complete) return 1;
    return 2;
}

void handle_write(Client* cltP, char* msg, int len){
    memcpy(cltP->wbuffer, msg, len);
    cltP->wlength = len;
    cltP->wbuffer[len] = '\0';
    cltP->pollout = POLLOUT;
}

void reset_client(Client* cltP) {
    cltP->rlength = 0;
    cltP->rbuffer[0] = '\0';
}

void init_client(int fdP, Client* cltP) {
    cltP->fd = fdP;
    cltP->rlength = 0;
    cltP->rbuffer[0] = '\0';
    cltP->wlength = 0;
    cltP->wbuffer[0] = '\0';
    cltP->pollout = 0;
    cltP->previous = NULL;
    cltP->next = NULL;
}

void disconnect_client(Client* cltP) {
    close(cltP->fd);
    cltP->previous->next = cltP->next;
    if(cltP->next != NULL) cltP->next->previous = cltP->previous;
    Client *temp = cltP;
    cltP = cltP->previous;
    free(temp);
    printf("client disconnected\n");
}

bool opr_read(Client* cltP){
    bool nonz_digit = false;
    int first_digit_pos = -1, last_digit_pos = -1;
    for(int i = 5; i < cltP->rlength; i++) {
        if(!isdigit(cltP->rbuffer[i])) {
            return false;
        }
        else{
            if(cltP->rbuffer[i] != '0'){
                if(!nonz_digit){
                    first_digit_pos = i;
                    nonz_digit = true;
                }
                last_digit_pos = i;
            }
        }
    }
    if(last_digit_pos - first_digit_pos >= 10){
        return false;
    }

    int idx = atoi(cltP->rbuffer + 5);
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

    handle_write(cltP, content, paragraph_length);

    return true;
}

bool opr_write(Client* cltP){
    int content_start = -1;
    for (int i = 7; i < cltP->rlength; i++) {
        if (!isdigit(cltP->rbuffer[i])) {
            if (cltP->rbuffer[i] == ' ') {
                cltP->rbuffer[i] = '\0';
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

    int idx = atoi(cltP->rbuffer + 7);
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
    int content_length = strlen(cltP->rbuffer + content_start);
    if (content_length > MESSAGE_SIZE) {
        content_length = MESSAGE_SIZE;
    }
    char content[content_length + 1];
    memmove(content, cltP->rbuffer + content_start, content_length);
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

    if(fdatasync(note) == -1){
        perror("sync note");
    }
    if(fdatasync(index) == -1){
        perror("sync index");
    }

    return true;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    int listen_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);
    unsigned short port = (unsigned short) atoi(argv[1]);
    char buffer[BUFFER_SIZE];

    listen_fd = init_server(port);
    if (listen_fd < 0) {
        fprintf(stderr, "Failed to initialize server.\n");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);

    int clt_num = 1;
    Client clt;
    Client *tail = &clt;
    struct pollfd fdarr[101];

    init_client(listen_fd, &clt);
    fdarr[0] = (struct pollfd){listen_fd, POLLIN, 0};
    int fdarr_it;

    while (true) {
        fdarr_it = 1;
        for(Client *it = clt.next; it != NULL; it = it->next, fdarr_it++){
            fdarr[fdarr_it] = (struct pollfd){it->fd, POLLIN | it->pollout, 0};
        }
        int totalFds = poll(fdarr, clt_num, -1);

        if(totalFds < 0){
            perror("poll");
        }
        else if(totalFds == 0){
            continue;
        }
        else{
            fdarr_it = 1;
            for(Client *it = clt.next; it != NULL; it = it->next, fdarr_it++){
                if(fdarr[fdarr_it].revents & POLLOUT){
                    write(it->fd, it->wbuffer, it->wlength);
                    it->pollout = 0;
                }
                if(fdarr[fdarr_it].revents & POLLIN){
                    int ret = handle_read(it);
                    if (ret == 1) {
                        printf("read from client: %s\n", it->rbuffer);
                        if(strncmp(it->rbuffer, "read ", 5) == 0) {
                            if(!opr_read(it)){
                                handle_write(it, "Invalid command", 15);
                            }
                            reset_client(it);
                        }
                        else if(strncmp(it->rbuffer, "update ", 7) == 0) {
                            if(!opr_write(it)){
                                handle_write(it, "Invalid command", 15);
                            }
                            reset_client(it);
                        }
                        else if(strncmp(it->rbuffer, "exit", 5) == 0) {
                            disconnect_client(it);
                            clt_num--;
                        }
                        else{
                            handle_write(it, "Invalid command", 15);
                            reset_client(it);
                        }
                    }
                    else if(ret <= 0){ // client disconnected or error
                        disconnect_client(it);
                        clt_num--;
                    }
                }
            }
            if(fdarr[0].revents & POLLIN){
                int new_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &addrlen);
                if (new_fd == -1) {
                    perror("accept");
                }
                Client *new_clt = (Client *)malloc(sizeof(Client));
                init_client(new_fd, new_clt);
                tail->next = new_clt;
                new_clt->previous = tail;
                tail = new_clt;
                clt_num++;
                printf("New connection from %s on socket %d\n", inet_ntoa(client_addr.sin_addr), new_fd);
            }
        }
        int totalFds = poll(fdarr, clt_num, -1);

        if(totalFds < 0){
            perror("poll");
        }
        else if(totalFds == 0){
            continue;
        }
        else{
            fdarr_it = 1;
            for(Client *it = clt.next; it != NULL; it = it->next, fdarr_it++){
                if(fdarr[fdarr_it].revents & POLLOUT){
                    write(it->fd, it->wbuffer, it->wlength);
                    it->pollout = 0;
                }
                if(fdarr[fdarr_it].revents & POLLIN){
                    int ret = handle_read(it);
                    if (ret == 1) {
                        printf("read from client: %s\n", it->rbuffer);
                        if(strncmp(it->rbuffer, "read ", 5) == 0) {
                            if(!opr_read(it)){
                                handle_write(it, "Invalid command", 15);
                            }
                            reset_client(it);
                        }
                        else if(strncmp(it->rbuffer, "update ", 7) == 0) {
                            if(!opr_write(it)){
                                handle_write(it, "Invalid command", 15);
                            }
                            reset_client(it);
                        }
                        else if(strncmp(it->rbuffer, "exit", 5) == 0) {
                            disconnect_client(it);
                            clt_num--;
                        }
                        else{
                            handle_write(it, "Invalid command", 15);
                            reset_client(it);
                        }
                    }
                    else if(ret <= 0){ // client disconnected or error
                        disconnect_client(it);
                        clt_num--;
                    }
                }
            }
            if(fdarr[0].revents & POLLIN){
                int new_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &addrlen);
                if (new_fd == -1) {
                    perror("accept");
                }
                Client *new_clt = (Client *)malloc(sizeof(Client));
                init_client(new_fd, new_clt);
                tail->next = new_clt;
                new_clt->previous = tail;
                tail = new_clt;
                clt_num++;
                printf("New connection from %s on socket %d\n", inet_ntoa(client_addr.sin_addr), new_fd);
            }
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
    if (listen(listen_fd, 10) == -1) { // Using 10 for backlog
        perror("listen");
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}