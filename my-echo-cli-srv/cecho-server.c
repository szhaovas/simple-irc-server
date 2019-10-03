#include <sys/socket.h> // socket(), bind(), listen(), accept()
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>  // socklen_t
#include <stdio.h>      // perror(), printf(), fprintf()
#include <stdlib.h>     // exit(), strtoul(), malloc()
#include <stdint.h>     // uint16_t
#include <string.h>     // memset(), strlen()
#include <unistd.h>     // close()
#include <fcntl.h>      // fcntl()
#include <sys/select.h> // select(), fd_set, etc.
#include <netdb.h>      // getnameinfo()
#include <errno.h>      // errno

#define MAX_LINE 512

/* Print error message |str| and exit if return code |__rc| < 0
 */
void exit_on_error(long __rc, const char* str)
{
    if (__rc < 0)
    {
        perror(str);
        exit(1);
    }
}

/* Print the hex code of the input |str| including '\0',
 * up to |max| characters (to prevent overflow).
 */
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

/* Build fd_set in |fds| given the listening socket |listenfd| and
 * file descriptors in the |conn| array with size |max_conn|
 */
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

/* Set file descriptor |fd| to be non-blocking.
 */
void set_non_blocking(int fd)
{
    // Get options
    int opts = fcntl(fd, F_GETFL);
    exit_on_error(opts, "fcntl(F_GETFL) failed");
    // Set options
    opts |= O_NONBLOCK;
    int __rc = fcntl(fd, F_SETFL, opts);
    exit_on_error(__rc, "fcntl(F_SETFL) fail");
}

/* Handle new incoming client connection on |listenfd| as reported by select()
 * If the connection can and has been accepted, then
 *   - update |conn| array to include this connection file descriptor,
 *   - increment the number of existing connections |num_conn| by 1.
 * 
 * The connection will be closed immediately after be accepted if
 *   - the number of existing connections has reached |max_conn|, or
 *   - we cannot retrieve client's hostname using getnameinfo().
 */
void handle_new_connection(int listenfd, int conn[], int* num_conn, int max_conn)
{
    struct sockaddr_in cli_addr;
    socklen_t cli_addr_len = sizeof(cli_addr);
    memset(&cli_addr, 0, cli_addr_len);
    int connfd = accept(listenfd, (struct sockaddr *) &cli_addr, &cli_addr_len);
    if (connfd < 0)
    {
        perror("accept() failed");
        return;
    }
    if (*num_conn == max_conn)
    {
        fprintf(stderr, "No room for new connections: %i\n", max_conn);
        close(connfd);
        return;
    }
    // Make fd non-blocking
    set_non_blocking(connfd);
    // Print new client's hostname info
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    if (getnameinfo((struct sockaddr *) &cli_addr, sizeof(cli_addr),
                    hbuf, sizeof(hbuf),
                    sbuf, sizeof(sbuf),
                    0))
    {
        perror("Could not get client's hostname");
        close(connfd);
        return;
    }
    fprintf(stderr, "New client from %s at port %s, fd=%i\n", hbuf, sbuf, connfd);
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

/* Handle new input data from file descriptor |connfd|, as reported by select()
 * Data will buffered in |rcv_buf| with size |rcv_buf_len|.
 */
int handle_data(int connfd, char rcv_buf[], int rcv_buf_len)
{
    memset(rcv_buf, 0, rcv_buf_len);
    // May not read all bytes?
    int bytes_read = read(connfd, rcv_buf, rcv_buf_len);
    if (bytes_read < 0)
    {
        // Nothing to read due to non-blocking fd
        if (errno == EAGAIN)
            return 0;
        // Something went wrong (connection reset by client, etc.)
        else
        {
            perror("read() failed");
            return -1;
        }
    }
    // Connection closed by client
    else if (bytes_read == 0)
    {
        fprintf(stderr, "EOF\n");
        return -1; // EOF
    }
    // Print received contents
    fprintf(stderr, "<< Got:   %s\n", rcv_buf);
    fprintf(stderr, "<< Bytes: ");
    print_hex(rcv_buf, rcv_buf_len);
    fprintf(stderr, "\n");
    // Echo everything back to client
    if (write(connfd, rcv_buf, strlen(rcv_buf)+1) < 0)
    {
        perror("write() failed");
        return -1;
    }
    // Print written contents (shouldn't change)
    fprintf(stderr, ">> Sent:  %s\n", rcv_buf);
    fprintf(stderr, ">> Bytes: ");
    print_hex(rcv_buf, rcv_buf_len);
    fprintf(stderr, "\n");
    return 0;
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
    
    char* srv_port_str = argv[1], *srv_port_str_end;
    unsigned long srv_port_long = strtoul(srv_port_str, &srv_port_str_end, 0);
    
    // Validate port range
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
    
    // Create listening socket
    listenfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    exit_on_error(listenfd, "socket() failed");
    
    // Enable address reuse
    const int reuse = 1;
    __rc = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    exit_on_error(__rc, "setsockopt() failed");
    
    // Make listening socket non-blocking
    set_non_blocking(listenfd);
    
    // Initialize sockaddr
    memset(&srv_addr, '\0', sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(srv_port);
    
    // Bind listening socket to specified port
    __rc = bind(listenfd, (struct sockaddr *) &srv_addr, sizeof(struct sockaddr));
    exit_on_error(__rc, "bind() failed");
    
    // Listen
    __rc = listen(listenfd, 1);
    exit_on_error(__rc, "listen() failed");
    fprintf(stderr, "Echo server started\n");
    
    // Set up file descriptors pool
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
    
    // Receive buffer (reused)
    char rcv_buf[MAX_LINE];
    int rcv_buf_len = MAX_LINE;
    
    while (1)
    {
        int highfd = build_fd_set(&fds, listenfd, conn, max_conn);
        int ready = select(highfd + 1,
                           &fds,
                           (fd_set *) 0,
                           (fd_set *) 0,
                           &timeout);
        exit_on_error(ready, "select() failed");
        
        if (ready == 0)
        {
            fprintf(stderr, ".");
            fflush(stdout);
        }
        else // ready > 0
        {
            // Accept a new connection
            if (FD_ISSET(listenfd, &fds))
            {
                handle_new_connection(listenfd, conn, &num_conn, max_conn);
            }
            // Check activities from connected sockets
            for (int i = 0; i < num_conn; i++)
            {
                if (FD_ISSET(conn[i], &fds))
                {
                    fprintf(stderr, "Active fd=%i\n", conn[i]);
                    __rc = handle_data(conn[i], rcv_buf, rcv_buf_len);
                    // If something went wrong,
                    // close this connection and remove associated state info
                    if (__rc < 0)
                    {
                        fprintf(stderr, "Client fd=%i closed the connection.\n", conn[i]);
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
