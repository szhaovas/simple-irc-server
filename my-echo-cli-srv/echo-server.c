#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/ip.h>


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
    if (argc != 2)
    {
        printf("Usage: ./echo-server PORT\n");
        exit(1);
    }
    unsigned short port = strtoul(argv[1], NULL, 10);
    
    // socket
    int listenfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    exit_on_error(listenfd, "socket() failed");
    
    // bind
    struct sockaddr_in saddr;
    memset(&saddr, '\0', sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    int rc = bind(listenfd, (struct sockaddr *) &saddr, sizeof(struct sockaddr));
    exit_on_error(rc, "bind() failed");
    
    // listen
    rc = listen(listenfd, 1);
    exit_on_error(rc, "listen() failed");
    printf("Echo server started\n");
    
    size_t buffer_size = 1024;
    char buf[buffer_size];
    
    // accept
    struct sockaddr_in caddr;
    
    while (1)
    {
        memset(&buf, 0, sizeof(buf)); // DON'T FORGET THIS
        memset(&caddr, 0, sizeof(buf)); // DON'T FORGET THIS
        
        socklen_t clen = sizeof(struct sockaddr_in);
        int connfd = accept(listenfd, (struct sockaddr *) &caddr, &clen);
        exit_on_error(connfd, "accept() failed");
        
        char client_addr_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &caddr.sin_addr.s_addr, (char *) client_addr_str, INET_ADDRSTRLEN);
        printf("Client from %s at port %u\n", client_addr_str, ntohs(caddr.sin_port));
        
        while (1)
        {
            memset(buf, 0, sizeof(buf)); // DON'T FORGET THIS
            int n_bytes = (int) read(connfd, &buf, buffer_size);
            exit_on_error(n_bytes, "read() failed");
            n_bytes = (int) write(connfd, buf, n_bytes);
            exit_on_error(n_bytes, "echo write() failed");
            printf("%s", buf);
        }
        
//        rc = close(connfd);
    }
    
//    exit(0);
}
