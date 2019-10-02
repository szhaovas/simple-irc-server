#include <sys/socket.h> // socket(), bind(), listen(), accept()
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>  // socklen_t
#include <stdio.h>      // perror(), printf(), fprintf()
#include <stdlib.h>     // exit(), strtoul()
#include <stdint.h>     // uint16_t
#include <string.h>     // memset(), strlen()
#include <unistd.h>     // close()

#define MAX_PENDING_CONNS 10
#define MAX_LINE 2


void exit_on_error(long rc, const char* str)
{
    if (rc < 0)
    {
        perror(str);
        exit(1);
    }
}


int main(int argc, char* argv[])
{
    int listenfd;
    struct sockaddr_in srv_addr;
    uint16_t srv_port;

    if (argc != 2)
    {
        printf("Usage: %s PORT\n", argv[0]);
        exit(1);
    }

    char* srv_port_str = argv[1];
    char* srv_port_str_end;
    unsigned long srv_port_long = strtoul(srv_port_str, &srv_port_str_end, 0);
    
    if (srv_port_str == srv_port_str_end // no conversion
        || *srv_port_str_end != '\0' // extra junk
        || srv_port_long < 1024 || srv_port_long > 65535) // out of range
    {
        fprintf(stderr,
            "Invalid port %s, please provide integer in 1024-65535 range\n",
             argv[1]);
        exit(1);
    }
    srv_port = (uint16_t) srv_port_long;

    
    // socket
    listenfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    exit_on_error(listenfd, "socket() failed");
    
    // bind
    memset(&srv_addr, '\0', sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(srv_port);

    int rc = bind(listenfd, (struct sockaddr *) &srv_addr, sizeof(struct sockaddr));
    exit_on_error(rc, "bind() failed");
    
    // listen
    rc = listen(listenfd, 1);
    exit_on_error(rc, "listen() failed");
    printf("Echo server started\n");
    
    char rcv_buf[MAX_LINE];
    
    // accept
    struct sockaddr_in cli_addr;
    socklen_t cli_addr_len = sizeof(cli_addr);
    
    while (1)
    {
        memset(&rcv_buf, 0, sizeof(rcv_buf)); // DON'T FORGET THIS
        memset(&cli_addr, 0, sizeof(cli_addr)); // DON'T FORGET THIS
        
        int connfd = accept(listenfd, (struct sockaddr *) &cli_addr, &cli_addr_len);
        exit_on_error(connfd, "accept() failed");
        
        char client_addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli_addr.sin_addr.s_addr, (char *) client_addr_str, INET_ADDRSTRLEN);
        printf("Client from %s at port %u\n", client_addr_str, ntohs(cli_addr.sin_port));
        
        while (1)
        {
            memset(rcv_buf, 0, sizeof(rcv_buf)); // DON'T FORGET THIS
            if (read(connfd, rcv_buf, sizeof(rcv_buf)) < 0)
            {
                perror("read() failed");
                break; // if continue, server will crash once client forces an exit
            }
            
            if (write(connfd, rcv_buf, strlen(rcv_buf)+1) < 0)
            {
                perror("write() failed");
            }
        }
        
        if (close(connfd) < 0)
            perror("close() failed");
    }

    close(listenfd);
}
