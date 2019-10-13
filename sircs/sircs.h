#ifndef _SIRCS_H_
#define _SIRCS_H_

#include <sys/types.h>
#include <netinet/in.h>
#include "linked-list.h"

#define MAX_CLIENTS 512
#define MAX_MSG_TOKENS 10
#define MAX_MSG_LEN 1024
#define MAX_USERNAME 32
#define MAX_HOSTNAME 64
#define MAX_SERVERNAME 64
#define MAX_REALNAME 192
#define MAX_CHANNAME 64

#define RFC_MAX_MSG_LEN 512
#define RFC_MAX_NICKNAME 9

typedef struct __client_struct client_t;
typedef struct __channel_struct channel_t;

typedef struct {
    char hostname[MAX_HOSTNAME];
    LinkedList* clients;
    LinkedList* channels;
    LinkedList* zombies;
} server_info_t;

struct __channel_struct {
    char name[MAX_CHANNAME];
    LinkedList* members;
};

struct __client_struct {
    int sock;
    struct sockaddr_in cliaddr;
    size_t inbuf_size;
    int keep_throwing;
    int registered;
    int zombie;
    char hostname[MAX_HOSTNAME];
    char servername[MAX_SERVERNAME]; // FIXME: not used, so can be removed
    char user[MAX_USERNAME];
    char nick[MAX_USERNAME];
    char realname[MAX_REALNAME];
    char inbuf[MAX_MSG_LEN+1];
    channel_t* channel;
};



int build_fd_set(fd_set* fds, int listenfd, LinkedList* clients);

int set_non_blocking(int fd);

int handle_new_connection(int listenfd, LinkedList* clients);

int handle_data(server_info_t* server_info, client_t* cli, Node* cli_node);

void exit_on_error(long __rc, const char* str);

#endif /* _SIRCS_H_ */
