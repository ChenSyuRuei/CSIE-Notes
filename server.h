#ifndef __SERVER_H
#define __SERVER_H

#define BACKLOG 10
#define BUFFER_SIZE 256
#define MESSAGE_SIZE 200
#define min(a, b) ((a) < (b) ? (a) : (b))
#define CHECK(syscall_expr) do { \
    if ((syscall_expr) == -1) { \
        fprintf(stderr, "System call error at %s:%d: %s\n", __FILE__, __LINE__, #syscall_expr); \
        perror(" -> "); \
        close(index); \
        close(note); \
        return 0; \
    } \
} while (0)

// Section Request
typedef struct client{
    int fd;
    size_t rlength;
    char rbuffer[BUFFER_SIZE];
    bool rbuffer_clamped;
    size_t wlength;
    size_t woffset;
    char wbuffer[BUFFER_SIZE];
    short pollout;
    int pidx;
} Client;

int handle_read(Client* cltP);
void handle_write(Client* cltP, char* msg, int len);
void reset_client(Client* cltP);
void init_client(int fdP, Client* cltP);
void disconnect_client(Client* cltP);
int opr_read(Client* cltP);
int opr_write(Client* cltP);

// Section Server
int init_server(unsigned short port);

#endif