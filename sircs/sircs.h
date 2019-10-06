#ifndef _SIRCS_H_
#define _SIRCS_H_

#include <sys/types.h>
#include <netinet/in.h>

#define MAX_CLIENTS 512
#define MAX_MSG_TOKENS 10
#define MAX_MSG_LEN 1024
#define MAX_USERNAME 32
#define MAX_HOSTNAME 64
#define MAX_SERVERNAME 64
#define MAX_REALNAME 192
#define MAX_CHANNAME 64

typedef struct{
  int sock;
  struct sockaddr_in cliaddr;
  unsigned inbuf_size;
  int registered;
  char hostname[MAX_HOSTNAME];
  char servername[MAX_SERVERNAME];
  char user[MAX_USERNAME];
  char nick[MAX_USERNAME];
  char realname[MAX_REALNAME];
  char inbuf[MAX_MSG_LEN+1];
  char channel[MAX_CHANNAME];
} client;


int build_fd_set(fd_set* fds, int listenfd, client* clients[], int max_clients);

int set_non_blocking(int fd);

int handle_new_connection(int listenfd, client* clients[], int* num_clients, int max_clients);

int handle_data(client* cli, char snd_buf[]);

void exit_on_error(long __rc, const char* str);

#endif /* _SIRCS_H_ */
