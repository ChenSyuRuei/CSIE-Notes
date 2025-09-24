#ifndef __SERVER_H
#define __SERVER_H

#define BACKLOG 10
#define BUFFER_SIZE 256
#define MESSAGE_SIZE 200
#define min(a, b) ((a) < (b) ? (a) : (b))
#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)

// Section Request
typedef struct {
    size_t length;
    char buffer[BUFFER_SIZE];
} request;

int handle_read(int fd, request* reqP);
void reset_request(request * reqP);

// Section Server
int init_server(unsigned short port);

#endif