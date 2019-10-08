#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>      // fcntl()
#include <errno.h>      // errno
#include <sys/select.h> // select(), fd_set, etc.

#include "sircs.h"
#include "debug.h"
#include "irc-proto.h"

#define NELMS(array) (sizeof(array) / sizeof(array[0]))

void usage() {
    eprintf("sircs [-h] [-D debugLevel] <port>\n");
    exit(-1);
}

char srv_hostname[MAX_HOSTNAME];
char snd_buf[RFC_MAX_MSG_LEN]; // server messages must conform to RFC

int main(int argc, char *argv[] ){
    DPRINTF(DEBUG_INIT, "Hello\n");
    extern char *optarg;
    extern int optind;
    int ch;
    
    while ((ch = getopt(argc, argv, "hD:")) != -1)
        switch (ch){
            case 'D':
                if (set_debug(optarg))
                    exit(-1);
                break;
            case 'h':
            default: /* FALLTHROUGH */
                usage();
        }
    
    argc -= optind;
    argv += optind;
    
    if (argc < 1) {
        usage();
    }
    
    char *endArg0Ptr;
    unsigned long portLong = strtoul(argv[0], &endArg0Ptr, 0);
    
    // port check: no conversion at all, extra junk at the end, out of range
    if (argv[0] == endArg0Ptr || *endArg0Ptr != '\0' ||
        portLong < 1024 || portLong > 65535){
        fprintf(stderr,
                "Invalid port %s, please provide integer in 1024-65535 range\n",
                argv[0]);
        exit(-1);
    }
    
    uint16_t port = (uint16_t) portLong;
    
    /* Initialize server */
    
    int __rc; // for return codes
    struct sockaddr_in srv_addr;
    
    // Create listening socket
    int listenfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    exit_on_error(listenfd, "socket() failed");
    
    // Enable address reuse
    const int reuse = 1;
    __rc = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    exit_on_error(__rc, "setsockopt() failed");
    
    // Make listening socket non-blocking
    __rc = set_non_blocking(listenfd);
    exit_on_error(__rc, "");
    
    
    // Initialize sockaddr
    memset(&srv_addr, '\0', sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(port);
    
    // Bind listening socket to the specified port
    __rc = bind(listenfd, (struct sockaddr *) &srv_addr, sizeof(struct sockaddr));
    exit_on_error(__rc, "bind() failed");
    
    // Listen
    __rc = listen(listenfd, 1);
    exit_on_error(__rc, "listen() failed");

    
    // Set up file descriptors pool
    fd_set fds;
    
    // Array to keep track of connections that are alive
    client *clients[MAX_CLIENTS];
    int num_clients = 0;
    memset(&clients, 0, sizeof(clients));
    
    // Time-out
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    
    // Allocate send buffer
    memset(snd_buf, '\0', RFC_MAX_MSG_LEN);
    snd_buf[0] = 'h';
    
    // Get server hostname
    srv_hostname[MAX_HOSTNAME-1] = '\0';
    gethostname(srv_hostname, sizeof(srv_hostname));
    
    DPRINTF(DEBUG_INIT, "Simple IRC server listening on %s:%d, fd=%d\n",
            srv_hostname,
            port,
            listenfd);
    
    // Start main server loop
    while (TRUE)
    {
        int highfd = build_fd_set(&fds, listenfd, clients);
        int ready = select(highfd + 1, &fds, (fd_set *) 0, (fd_set *) 0, &timeout);
        exit_on_error(ready, "select() failed");
        
        if (ready == 0)
        {
            DPRINTF(DEBUG_CLIENTS, ".");
            fflush(stdout);
        }
        else // ready > 0
        {
            // Accept a new connection
            if (FD_ISSET(listenfd, &fds))
            {
                handle_new_connection(listenfd, clients, &num_clients);
            }
            // Check activities from connected sockets
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (clients[i] != NULL)
                {
                    int sock = clients[i]->sock;
                    if (FD_ISSET(sock, &fds))
                    {
                        DPRINTF(DEBUG_CLIENTS, "Active fd=%i\n", sock);
                        __rc = handle_data(clients, i, snd_buf);
                        // If something went wrong,
                        // close this connection and remove associated state info
                        if (__rc < 0)
                        {
                            DPRINTF(DEBUG_CLIENTS, "Client fd=%i closed the connection.\n", sock);
                            free(clients[i]);
                            clients[i] = NULL;
                            num_clients -= 1;
                        }
                    }
                }
            }
        }
    }
    close(listenfd);
    
    return 0;
}


char* get_server_hostname(char* buf)
{
    return stpcpy(buf, srv_hostname);
}



/* Build fd_set in |fds| given the listening socket |listenfd| and
 * client sockets |clients| array.
 */
int build_fd_set(fd_set *fds, int listenfd, client *clients[])
{
    int highfd = listenfd;
    FD_ZERO(fds);
    FD_SET(listenfd, fds);
    // update highfd
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        client *cli = clients[i];
        if (cli != NULL)
        {
            int fd = cli->sock;
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
int set_non_blocking(int fd)
{
    // Get options
    int opts = fcntl(fd, F_GETFL);
    if (opts < 0)
    {
        perror("fcntl(F_GETFL) failed");
        return -1;
    }
    // Set options
    opts |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, opts) < 0)
    {
        perror("fcntl(F_SETFL) fail");
        return -1;
    }
    return 0;
}



/* Handle new incoming client connection on |listenfd| as reported by select()
 * If the connection can and has been accepted, then
 *   - update |clients| array to record this client's info,
 *   - increment the number of existing connections |num_clients| by 1.
 *
 * The connection will be closed immediately after being accepted if
 *   - the number of existing connections has reached |MAX_CLIENTS|, or
 *   - cannot set connection socket to be non-blocking
 *   - cannot retrieve client's hostname using getnameinfo().
 */
int handle_new_connection(int listenfd,
                          client *clients[],
                          int *num_clients)
{
    // Accept any new connection
    struct sockaddr_in cli_addr;
    socklen_t cli_addr_len = sizeof(cli_addr);
    memset(&cli_addr, 0, cli_addr_len);
    int sock = accept(listenfd, (struct sockaddr *) &cli_addr, &cli_addr_len);
    if (sock < 0)
    {
        perror("accept() failed");
        return -1;
    }
    if (*num_clients == MAX_CLIENTS)
    {
        DPRINTF(DEBUG_SOCKETS, "No room for new connections\n");
        close(sock);
        return -1;
    }
    
    // Ready to record client information
    client *cli = (client *) malloc(sizeof(client));
    memset(cli, 0, sizeof(*cli));
    
    
    // Initialize connection socket
    if (set_non_blocking(sock) < 0)
    {
        close(sock);
        return -1;
    }
    cli->sock = sock;
    
    // Reverse lookup client's hostname
    char host_buf[NI_MAXHOST], serv_buf[NI_MAXSERV];
    if (getnameinfo((struct sockaddr *) &cli_addr, sizeof(cli_addr),
                    host_buf, sizeof(host_buf),
                    serv_buf, sizeof(serv_buf),
                    0))
    {
        perror("Failed to reverse lookup client's hostname");
        close(sock);
        return -1;
    }
    
    // Initialize hostname (always NUL-terminated), and truncate if necessary
    // TESTME
    strncpy(cli->hostname, host_buf, MIN( strlen(host_buf),
                                          MAX_HOSTNAME-1 ));  // -1 for the last '\0'
    
    // Initialize various fields
    memcpy(&(cli->cliaddr), &cli_addr, sizeof(cli_addr));
    cli->inbuf_size = 0;
    
    DPRINTF(DEBUG_CLIENTS, "New client from %s, fd=%i\n",
            cli->hostname,
            cli->sock);
    
    // Look for an empty slot to hold this client's record
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] == NULL)
        {
            clients[i] = cli;
            *num_clients += 1;
            break;
        }
    }
    return 0;
}




/* Handle new input data from the client.
 */
int handle_data(client* clients[], int client_no, char* snd_buf)
{
    client *cli = clients[client_no];
    // Make sure there's still space to read anything
    assert(cli->inbuf_size < MAX_MSG_LEN);
    // Compute remaning space
    int buf_remaining = MAX_MSG_LEN - cli->inbuf_size;
    // Compute buffer offset
    char *buf = cli->inbuf + cli->inbuf_size;
    int bytes_read = (int) read(cli->sock, buf, buf_remaining);
    if (bytes_read < 0)
    {
        // Nothing to read due to non-blocking fd
        if (errno == EAGAIN)
            return 0;
        // Something else went wrong (connection reset by client, etc.)
        else
        {
            DEBUG_PERROR("read() failed");
            return -1;
        }
    }
    // Connection closed by client (EOF)
    else if (bytes_read == 0)
    {
        DPRINTF(DEBUG_INPUT, "EOF\n");
        return -1;
    }
    // Print received contents
    DPRINTF(DEBUG_INPUT, "<< Got:   %s\n", buf);
    DPRINTF(DEBUG_INPUT, "<< Bytes: ");
    print_hex(DEBUG_INPUT, buf, MAX_MSG_LEN+1);
    DPRINTF(DEBUG_INPUT, "\n");
    
    char *msg = cli->inbuf; // start of a (potential) msg
    char *cr, *lf, *end;
    while (msg < cli->inbuf + MAX_MSG_LEN) {
        cr = strchr(msg, '\r');
        lf = strchr(msg, '\n');
        if (!cr && !lf)
            break;
        if (!cr)
            end = lf;
        else if (!lf)
            end = cr;
        else
            end = MIN(cr, lf);
        *end = '\0';
        
        /* Replace the following with handleLine(msg, cli); */
        // Replace start
        DPRINTF(DEBUG_INPUT, "Msg: %s\n", msg);
        print_hex(DEBUG_INPUT, msg, MAX_MSG_LEN);
        DPRINTF(DEBUG_INPUT, "\n");
        // Replace end
        handleLine(msg, clients, client_no);
        
        msg = end + 1;
    }
    size_t last_msg_len = strlen(msg); //
    // Nothing else to read
    // Also throw away a segment if it is already too long
    if ( last_msg_len == 0 || last_msg_len >= RFC_MAX_MSG_LEN)
    {
        memset(&(cli->inbuf), '\0', MAX_MSG_LEN);
        cli->inbuf_size = 0;
    }
    // Initial segment of an incomplete msg (assuming the rest will be delivered next time)
    // Move this segment to the beginning of the buffer
    else
    {
        strcpy(snd_buf, msg); // use send buffer as intermediate storage
        memset(&(cli->inbuf), '\0', MAX_MSG_LEN);
        strcpy(cli->inbuf, snd_buf);
        memset(snd_buf, '\0', MAX_MSG_LEN);
        cli->inbuf_size = (unsigned) strlen(cli->inbuf);
        
        DPRINTF(DEBUG_INPUT, "Incomplete msg: %s\n", cli->inbuf);
        print_hex(DEBUG_INPUT, cli->inbuf, MAX_MSG_LEN);
        DPRINTF(DEBUG_INPUT, "\n");
    }
    return 0;
}



/* Print error message |str| and exit if return code |__rc| < 0
 */
void exit_on_error(long __rc, const char *str)
{
    if (__rc < 0)
    {
        perror(str);
        exit(1);
    }
}
