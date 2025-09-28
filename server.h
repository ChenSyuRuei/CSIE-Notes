#ifndef __SERVER_H
#define __SERVER_H

#define BACKLOG 10
#define BUFFER_SIZE 256
#define MESSAGE_SIZE 200
#define min(a, b) ((a) < (b) ? (a) : (b))
#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)

// Section Request
typedef struct client{
    int fd;
    size_t rlength;
    char rbuffer[BUFFER_SIZE];
    size_t wlength;
    char wbuffer[BUFFER_SIZE];
    short pollout;
    struct client *previous;
    struct client *next;
} Client;

int handle_read(Client* cltP);
void handle_write(Client* cltP, char* msg, int len);
void reset_client(Client* cltP);
void init_client(int fdP, Client* cltP);
void disconnect_client(Client* cltP);
bool opr_read(Client* cltP);
bool opr_write(Client* cltP);

// Section Server
int init_server(unsigned short port);

#endif