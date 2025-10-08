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
#include <sys/stat.h>
#include <arpa/inet.h>
#include <errno.h>

#include "server.h"

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef CHECK
#define CHECK(syscall_expr) do { \
    if ((syscall_expr) == -1) { \
        fprintf(stderr, "System call error at %s:%d: %s\n", __FILE__, __LINE__, #syscall_expr); \
        perror(" -> "); \
        close(index); \
        close(note); \
        return false; \
    } \
} while (0)
#endif

const char IAC_IP[3] = "\xff\xf4";

int paragraph_total = 0;

int handle_read(Client* cltP) {
    /*
        return value:
        -1: read failed
        0: EOF (client has disconnected)
        1: read a whole command
        2: only read a fragment of command, need more reading
    */

    bool command_complete = false;
    int msg_length;
    int store_len;
    int space_left = BUFFER_SIZE - cltP->rlength - 1; // minus 1 for null-terminator
    char buffer[BUFFER_SIZE];    

    memset(buffer, 0, sizeof(buffer));
    msg_length = read(cltP->fd, buffer, sizeof(buffer));
    if (msg_length == -1) return -1;
    if (msg_length == 0) return 0;
    if (space_left == 0) cltP->rbuffer_clamped = true;

    char* p1 = strstr(buffer, "\015\012"); // \r\n
    if (p1 == NULL) {
        p1 = strstr(buffer, "\012"); // \n
        if (p1 == NULL) {
            if (!strncmp(buffer, IAC_IP, 2)) {
                return 0;
            }
        } else command_complete = true;
    } else command_complete = true;

    // restore the coming message in buffer
    store_len = min((p1 == NULL) ? msg_length : (p1 - buffer), space_left);
    memcpy(cltP->rbuffer + cltP->rlength, buffer, store_len);
    cltP->rlength += store_len;
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
    cltP->rbuffer_clamped = false;
}

void init_client(int fdP, Client* cltP) {
    cltP->fd = fdP;
    cltP->rlength = 0;
    cltP->rbuffer[0] = '\0';
    cltP->rbuffer_clamped = false;
    cltP->wlength = 0;
    cltP->wbuffer[0] = '\0';
    cltP->pollout = 0;
    cltP->previous = NULL;
    cltP->next = NULL;
}

void disconnect_client(Client* cltP) {
    close(cltP->fd);
    cltP->previous->next = cltP->next;
    if (cltP->next != NULL) cltP->next->previous = cltP->previous;
    //printf("Client %d disconnected.\n", cltP->fd);

    Client *temp = cltP;
    cltP = cltP->previous; // for next iteration in main loop
    free(temp);
}

bool opr_read(Client* cltP){
    // check if paragraph parameter is valid
    for(int i = 5; i < cltP->rlength; i++) {
        if(!isdigit(cltP->rbuffer[i])) {
            return false;
        }
    }
    errno = 0;
    char *endptr;
    long idx = strtol(cltP->rbuffer + 5, &endptr, 10);
    if (errno != 0 || endptr == cltP->rbuffer + 5 || *endptr != '\0' || idx < 0 || idx >= paragraph_total || cltP->rbuffer_clamped) return false;

    // open note file
    int note = open("./note.txt", O_RDONLY);
    int index = open("./index", O_RDONLY);
    if (note < 0 || index < 0) {
        if (note >= 0) close(note);
        if (index >= 0) close(index);
        return false;
    }

    // acquire read lock on [0, idx] in index
    struct flock rdlock_idx;
    memset(&rdlock_idx, 0, sizeof(rdlock_idx));
    rdlock_idx.l_type = F_RDLCK;
    rdlock_idx.l_whence = SEEK_SET;
    rdlock_idx.l_start = 0;
    rdlock_idx.l_len = idx + 1;
    CHECK(fcntl(index, F_SETLKW, &rdlock_idx));

    // get paragraph length and its start at note
    int head_length = 0, paragraph_length = -1;
    unsigned char hex;
    CHECK(lseek(index, 0, SEEK_SET));
    for (int i = 0; i <= idx; i++){
        CHECK(read(index, &hex, 1));
        int value = (int)hex;
        if(i == idx){
            paragraph_length = value;
            break;
        }
        head_length += value;
    }

    // unlock read lock on [0, idx) in index
    struct flock unlock_idx;
    memset(&unlock_idx, 0, sizeof(unlock_idx));
    unlock_idx.l_type = F_UNLCK;
    unlock_idx.l_whence = SEEK_SET;
    unlock_idx.l_start = 0;
    unlock_idx.l_len = idx;
    CHECK(fcntl(index, F_SETLK, &unlock_idx));

    // acquire read lock on [head_length, head_length + paragraph_length] in index
    struct flock rdlock_note;
    memset(&rdlock_note, 0, sizeof(rdlock_note));
    rdlock_note.l_type = F_RDLCK;
    rdlock_note.l_whence = SEEK_SET;
    rdlock_note.l_start = head_length;
    rdlock_note.l_len = paragraph_length;
    CHECK(fcntl(note, F_SETLKW, &rdlock_note));

    //sleep(5);
    //printf("client %d read\n", cltP->fd);

    // set content
    char content[paragraph_length + 1];
    CHECK(pread(note, content, paragraph_length, head_length));
    content[paragraph_length] = '\0';

    handle_write(cltP, content, paragraph_length);

    close(note);
    close(index);
    return true;
}

bool opr_write(Client* cltP){
    // check if paragraph parameter is valid
    int content_start = -1;
    for (int i = 7; i < cltP->rlength; i++) {
        if (!isdigit(cltP->rbuffer[i])) {
            if (cltP->rbuffer[i] == ' ') {
                cltP->rbuffer[i] = '\0';
                content_start = i + 1;
                break;
            } else {
                return false;
            }
        }
    }

    errno = 0;
    char *endptr;
    long idx = strtol(cltP->rbuffer + 7, &endptr, 10);
    if (errno != 0 || endptr == cltP->rbuffer + 7 || *endptr != '\0' || idx < 0 || idx >= paragraph_total || content_start == -1) return false;

    // open note file
    int note = open("./note.txt", O_RDWR);
    int index = open("./index", O_RDWR);
    if (note < 0 || index < 0) {
        if (note >= 0) close(note);
        if (index >= 0) close(index);
        return false;
    }

    // acquire read lock on [0, idx+1) in index
    struct flock rlock;
    memset(&rlock, 0, sizeof(rlock));
    rlock.l_type = F_RDLCK;
    rlock.l_whence = SEEK_SET;
    rlock.l_start = 0;
    rlock.l_len = idx + 1;
    CHECK(fcntl(index, F_SETLKW, &rlock));

    // get paragraph length and its start at note
    int head_length = 0, paragraph_length = -1;
    unsigned char hex;
    CHECK(lseek(index, 0, SEEK_SET));
    for (int i = 0; i <= idx; i++){
        CHECK(read(index, &hex, 1));
        int value = (int)hex;
        if(i == idx){
            paragraph_length = value;
            break;
        }
        head_length += value;
    }

    // unlock read lock on [0, idx) in index
    struct flock unlock;
    memset(&unlock, 0, sizeof(unlock));
    unlock.l_type = F_UNLCK;
    unlock.l_whence = SEEK_SET;
    unlock.l_start = 0;
    unlock.l_len = idx;
    CHECK(fcntl(index, F_SETLK, &unlock));

    // define different types of lengths
    int content_length = min(strlen(cltP->rbuffer + content_start), MESSAGE_SIZE);
    char content[content_length + 1];
    memmove(content, cltP->rbuffer + content_start, content_length);
    content[content_length] = '\0';

    int file_length = lseek(note, 0, SEEK_END);
    CHECK(file_length);

    int tail_length = file_length - head_length - paragraph_length;
    char tail_content[tail_length + 1];

    // blocking lock
    if (content_length == paragraph_length) { // If size remains the same (A), acquire write lock on [idx, idx] in index, [head_length, head_length + content_length] in note
        struct flock wrlock_note;
        memset(&wrlock_note, 0, sizeof(wrlock_note));
        wrlock_note.l_type = F_WRLCK;
        wrlock_note.l_whence = SEEK_SET;
        wrlock_note.l_start = head_length;
        wrlock_note.l_len = content_length;
        CHECK(fcntl(note, F_SETLKW, &wrlock_note));

        //sleep(5);
        //printf("client %d write\n", cltP->fd);
        CHECK(pwrite(note, content, content_length, head_length));
    }
    else { // If size changes (B), acquire write lock on [idx, inf) in index, [head_length, inf) in note 
        struct flock wrlock_index;
        memset(&wrlock_index, 0, sizeof(wrlock_index));
        wrlock_index.l_type = F_WRLCK;
        wrlock_index.l_whence = SEEK_SET;
        wrlock_index.l_start = idx;
        wrlock_index.l_len = 0;
        CHECK(fcntl(index, F_SETLKW, &wrlock_index));

        struct flock wrlock_note;
        memset(&wrlock_note, 0, sizeof(wrlock_note));
        wrlock_note.l_type = F_WRLCK;
        wrlock_note.l_whence = SEEK_SET;
        wrlock_note.l_start = head_length;
        wrlock_note.l_len = 0;
        CHECK(fcntl(note, F_SETLKW, &wrlock_note));

        //sleep(5);
        //printf("client %d write\n", cltP->fd);
        unsigned char new_hex = (unsigned char)content_length;
        CHECK(pwrite(index, &new_hex, 1, idx));
        CHECK(pread(note, tail_content, tail_length, head_length + paragraph_length));
        tail_content[tail_length] = '\0';
        CHECK(pwrite(note, content, content_length, head_length));
        CHECK(pwrite(note, tail_content, tail_length, head_length + content_length));

        if (paragraph_length > content_length) ftruncate(note, head_length + content_length + tail_length);
    }
    
    if (fdatasync(note) == -1) perror("sync note");
    if (fdatasync(index) == -1) perror("sync index");

    close(note);
    close(index);
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
    struct pollfd fdarr[101] = {(struct pollfd){listen_fd, POLLIN, 0}};
    int fdarr_it;
    struct stat st;
    if (stat("./index", &st) == -1) exit(EXIT_FAILURE);
    paragraph_total = st.st_size;

    init_client(listen_fd, &clt);

    while (true) {
        fdarr_it = 1;
        for (Client *it = clt.next; it != NULL; it = it->next, fdarr_it++) {
            fdarr[fdarr_it] = (struct pollfd){it->fd, POLLIN | it->pollout, 0};
        }
        int totalFds = poll(fdarr, clt_num, -1);

        if (totalFds < 0) perror("poll");
        else if (totalFds == 0) continue;
        else {
            fdarr_it = 1;
            for (Client *it = clt.next; it != NULL; it = it->next, fdarr_it++) {
                if (fdarr[fdarr_it].revents & POLLOUT) {
                    write(it->fd, it->wbuffer, it->wlength);
                    it->pollout = 0;
                }
                if (fdarr[fdarr_it].revents & POLLIN) {
                    int ret = handle_read(it);
                    if (ret == 1) {
                        if (strncmp(it->rbuffer, "read ", 5) == 0 && it->pollout == 0) {
                            if (!opr_read(it)) {
                                handle_write(it, "Invalid command", 15);
                            }
                            reset_client(it);
                        } else if (strncmp(it->rbuffer, "update ", 7) == 0) {
                            if (!opr_write(it)){
                                handle_write(it, "Invalid command", 15);
                            }
                            reset_client(it);
                        } else if (strncmp(it->rbuffer, "exit", 5) == 0) {
                            disconnect_client(it);
                            clt_num--;
                        } else {
                            handle_write(it, "Invalid command", 15);
                            reset_client(it);
                        }
                    } else if(ret <= 0) {
                        disconnect_client(it);
                        clt_num--;
                    }
                }
            }
            if (fdarr[0].revents & POLLIN) {
                int new_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &addrlen);
                if (new_fd == -1) perror("accept");

                Client *new_clt = (Client *)malloc(sizeof(Client));
                init_client(new_fd, new_clt);
                tail->next = new_clt;
                new_clt->previous = tail;
                tail = new_clt;
                clt_num++;

                //printf("New connection from %s on socket %d\n", inet_ntoa(client_addr.sin_addr), new_fd);
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