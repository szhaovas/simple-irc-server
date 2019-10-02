#include <sys/socket.h> // socket(), bind(), listen(), accept()
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>  // socklen_t
#include <stdio.h>      // perror(), printf(), fprintf()
#include <stdlib.h>     // exit(), strtoul(), malloc()
#include <stdint.h>     // uint16_t
#include <string.h>     // memset(), strlen()
#include <unistd.h>     // close()
#include <fcntl.h>      // fcntl
#include <sys/select.h> // select(), fd_set, etc.

#define MAX_PENDING_CONNS 10
#define MAX_LINE 1024


void exit_on_error(long __rc, const char* str)
{
    if (__rc < 0)
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
            fprintf(stderr, "\\0");
            break;
        }
        else
        {
            fprintf(stderr, "0x%x ", str[i]);
        }
    }
}


int build_fd_set
(fd_set* fds, int listenfd, int conn[], int max_conn)
{
    int highfd = listenfd;
    FD_ZERO(fds);
    FD_SET(listenfd, fds);
    // update highfd
    for (int i = 0; i < max_conn; i++)
    {
        int fd = conn[i];
        if (fd > 0)
        {
            FD_SET(fd, fds);
            if (fd > highfd)
            {
                highfd = fd;
            }
        }
    }
    return highfd;
}

void set_non_blocking(int fd)
{
    int opts = fcntl(fd, F_GETFL);
    exit_on_error(opts, "fcntl(F_GETFL) failed");
    opts |= O_NONBLOCK;
    int __rc = fcntl(fd, F_SETFL, opts);
    exit_on_error(__rc, "fcntl(F_SETFL) fail");
}

void handle_new_connection(int listenfd, int conn[], int* num_conn, int max_conn)
{
    struct sockaddr_in cli_addr;
    socklen_t cli_addr_len = sizeof(cli_addr);
    memset(&cli_addr, 0, cli_addr_len);
    int connfd = accept(listenfd, (struct sockaddr *) &cli_addr, &cli_addr_len);
    if (connfd < 0)
    {
        perror("accept() failed");
    }
    if (*num_conn == max_conn)
    {
        fprintf(stderr, "No room for new connections: %i\n", max_conn);
        close(connfd);
        return;
    }
    // Make fd non-blocking
    set_non_blocking(connfd);
    // Annouce the new client
    char client_addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli_addr.sin_addr.s_addr, (char *) client_addr_str, INET_ADDRSTRLEN);
    fprintf(stderr, "New client from %s at port %u\n", client_addr_str, ntohs(cli_addr.sin_port));
    // Look for an empty slot to record this fd
    for (int i = 0; i < max_conn; i++)
    {
        if (conn[i] == 0)
        {
            conn[i] = connfd;
            *num_conn += 1;
            break;
        }
    }
}

int handle_data(int connfd, char rcv_buf[], int rcv_buf_len)
{
    memset(rcv_buf, 0, rcv_buf_len); // DON'T FORGET THIS
    int bytes_read;
    
    // May not read all bytes?
    bytes_read = read(connfd, rcv_buf, rcv_buf_len);
    
    if (bytes_read < 0)
    {
        perror("read() failed");
        return -1;
    }
    if (bytes_read == 0)
    {
        // Nothing to read (hash collision?)
        return 0;
    }
    
    fprintf(stderr, "<< Got:   %s\n", rcv_buf);
    fprintf(stderr, "<< Bytes: ");
    print_hex(rcv_buf, rcv_buf_len);
    fprintf(stderr, "\n");
    
    if (write(connfd, rcv_buf, strlen(rcv_buf)+1) < 0)
    {
        perror("write() failed");
        return -1;
    }
    
    fprintf(stderr, ">> Sent:  %s\n", rcv_buf);
    fprintf(stderr, ">> Bytes: ");
    print_hex(rcv_buf, rcv_buf_len);
    fprintf(stderr, "\n");
    return 0;
    // __rc = close(connfd);
    // exit_on_error(__rc, "close(connection) failed");
}

int main(int argc, char* argv[])
{
    int __rc;
    int listenfd;
    struct sockaddr_in srv_addr;
    uint16_t srv_port;
    
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s PORT\n", argv[0]);
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
    
    // Enable address reuse
    const int reuse = 1;
    __rc = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    exit_on_error(__rc, "setsockopt() failed");
    
    // Make listening socket non-blocking
    set_non_blocking(listenfd);
    
    // bind
    memset(&srv_addr, '\0', sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(srv_port);
    
    __rc = bind(listenfd, (struct sockaddr *) &srv_addr, sizeof(struct sockaddr));
    exit_on_error(__rc, "bind() failed");
    
    // listen
    __rc = listen(listenfd, MAX_PENDING_CONNS);
    exit_on_error(__rc, "listen() failed");
    fprintf(stderr, "Echo server started with fd=%i\n", listenfd);
    
    // Set up fd pool
    fd_set fds;
    
    // Array to keep track of connections that are alive
    int conn[FD_SETSIZE];
    int max_conn = 2;
    int num_conn = 0;
    memset(&conn, 0, sizeof(conn));
    
    // Time-out
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    
    char rcv_buf[MAX_LINE];
    int rcv_buf_len = MAX_LINE;
    
    while (1)
    {
        int highfd = build_fd_set(&fds, listenfd, conn, max_conn);
        int ready = select(highfd + 1,
                           &fds,
                           (fd_set *) 0,
                           (fd_set *) 0,
                           NULL);
        exit_on_error(ready, "select() failed");
        
        if (ready == 0)
        {
            fprintf(stderr, ".");
            fflush(stdout);
        }
        else // ready > 0
        {
            // accept a new connection
            if (FD_ISSET(listenfd, &fds))
            {
                handle_new_connection(listenfd, conn, &num_conn, max_conn);
            }
            // check activities from connected sockets
            for (int i = 0; i < highfd; i++)
            {
                if (FD_ISSET(conn[i], &fds)){
                    __rc = handle_data(conn[i], rcv_buf, rcv_buf_len);
                    // read() or write() failed (assuming it was due to "connection reset by peer")
                    // Close this connection and remove associated state information
                    if (__rc < 0)
                    {
                        close(conn[i]);
                        conn[i] = 0;
                        num_conn -= 1;
                    }
                }
            }
        }
    }
    
    close(listenfd);
}
