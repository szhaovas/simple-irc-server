#include <sys/types.h>  // getaddrinfo()
#include <sys/socket.h> // socket(), connect(), getaddrinfo()
#include <arpa/inet.h> // inet_ntop()
#include <netinet/in.h> // sockaddr_in
#include <netdb.h>      // getaddrinfo()
#include <stdio.h>      // perror()
#include <stdlib.h>     // exit()
#include <stdint.h>     // uint16_t
#include <string.h>     // memset(), strlen()
#include <unistd.h>     // read(), write()

#define MAX_LINE 2
#define SRV_ADDR "127.0.0.1"

void exit_on_error(int rc, const char* str)
{
    if (rc < 0)
    {
        perror(str);
        exit(1);
    }
}

void print_hex(char* str, int max)
{
    for (int i = 0; i < max; i++)
    {
        if (str[i] == '\0')
        {
            printf("\\0");
            break;
        }
        else
        {
            printf("0x%x ", str[i]);
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        // Don't fix the progam name. Use argv[0] instead.
        // Also, print to stderr, not stdout
        // MYCODE:
        // printf("Usage: ./echo-server PORT\n");
        fprintf(stderr, "Usage:  %s <srvPort>\n", argv[0]);
        exit(1);
    }
    int rc;

    // Never trust user input. Always verify it!
    // Two ways this could go wrong:
    //   - The port string doesn't contain any number, or extra junk
    //     To fix this, (see man strtoul) also pass in endptr, which points to
    //     the first invalid character
    //   - The port is in an invalid range (not too high, not too low)
    // MYCODE:
    // unsigned short port = strtoul(argv[1], NULL, 10);
    char* srv_port_str = argv[1];
    char* srv_port_str_end;
    unsigned long srv_port_long = strtoul(srv_port_str, &srv_port_str_end, 0);
    // Two things:
    //   - The 2nd arg is char**, since it needs to modify a char* var
    //   - 0 base is assumed to be base 10
    
    if (srv_port_str == srv_port_str_end // no conversion at all
        || *srv_port_str_end != '\0'     // extra junk at the end
        || srv_port_long < 1024               // priviledged ports
        || srv_port_long >= 65536)            // too high
    {
        fprintf(stderr,
            "Invalid port %s, please provide integer in 1024-65535 range\n",
             srv_port_str);
        exit(1);
    }

    // Use getaddrinfo to query DNS to get address
    struct addrinfo* srv_addr_info;
    rc = getaddrinfo(SRV_ADDR, srv_port_str, NULL, &srv_addr_info);
    exit_on_error(rc, "getaddrinfo() failed");
    // MYCODE:
    // inet_pton(AF_INET, SRV_ADDR, &saddr.sin_addr.s_addr);
    
    // socket
    int connfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    exit_on_error(connfd, "socket() failed");
    
    // connect
    rc = connect(connfd, srv_addr_info->ai_addr, srv_addr_info->ai_addrlen);
    exit_on_error(rc, "connect() failed");

    freeaddrinfo(srv_addr_info); // Don't forget this!
    
    
    // size_t buffer_size = 1024; // use macro for constants
    char snd_buf[MAX_LINE];
    char rcv_buf[MAX_LINE];
    while (1)
    {
        memset(snd_buf, 0, sizeof(snd_buf));
        memset(rcv_buf, 0, sizeof(rcv_buf));

        if (fgets(snd_buf, sizeof(snd_buf), stdin) == NULL) // nothing read
            continue;

        rc = write(connfd, snd_buf, strlen(snd_buf)+1);
        printf("Sent:  %s\n", snd_buf);
        printf("Bytes: ");
        print_hex(snd_buf, sizeof(snd_buf));
        printf("\n");
        // strlen+1 bytes of data including '\0' !!!
        if (rc < 0)
        {
            perror("write() failed");
            break; // don't exit yet
        }
        
        rc = read(connfd, &rcv_buf, sizeof(rcv_buf));
        if (rc < 0)
        {
            perror("read() failed");
            break; // don't exit yet
        }
        
        printf("Got:   %s\n", rcv_buf);
        printf("Bytes: ");
        print_hex(rcv_buf, sizeof(rcv_buf));
        printf("\n");
    
    }
    
    close(connfd);
}
