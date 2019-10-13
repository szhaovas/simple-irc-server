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
#include <signal.h>

#include "sircs.h"
#include "debug.h"
#include "irc-proto.h"


void usage() {
    eprintf("sircs [-h] [-D debugLevel] <port>\n");
    exit(-1);
}



int main(int argc, char *argv[] ){
    
    signal(SIGPIPE, SIG_IGN); /* Block SIGPIPE Signals */
    
    DEBUG_PRINTF(DEBUG_INIT, "Hello\n");
    
    // Parse args
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
    
    // Port check: no conversion at all, extra junk at the end, out of range
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
    
    // Time-out
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    
    
    /* Initialize server_info struct */
    server_info_t server_info;
    memset(&server_info, '\0', sizeof(server_info));
    
    // Get server hostname
    size_t hostname_len = sizeof(server_info.hostname);
    server_info.hostname[hostname_len-1] = '\0';
    gethostname(server_info.hostname, hostname_len-1);
    
    // Client list
    LinkedList* clients = malloc(sizeof(LinkedList));
    init_list(clients);
    server_info.clients = clients;
    
    // Zombie client list
    LinkedList* zombies = malloc(sizeof(LinkedList));
    init_list(zombies);
    server_info.zombies = zombies;
    
    // Channel list
    LinkedList* channels = malloc(sizeof(LinkedList));
    init_list(channels);
    server_info.channels = channels;
    
    DEBUG_PRINTF(DEBUG_INIT, "Simple IRC server listening on %s:%d, fd=%d\n",
            server_info.hostname,
            port,
            listenfd);
    
    // Start main server loop
    while (TRUE)
    {
        int highfd = build_fd_set(&fds, listenfd, server_info.clients);
        int ready  = select(highfd + 1, &fds, (fd_set *) 0, (fd_set *) 0, &timeout);
        exit_on_error(ready, "select() failed");
        
        if (ready == 0)
        {
            DEBUG_PRINTF(DEBUG_CLIENTS, ".");
            fflush(stdout);
        }
        else // ready > 0
        {
            DEBUG_PRINTF(DEBUG_CLIENTS, "\n");
            // Accept a new connection
            if (FD_ISSET(listenfd, &fds))
            {
                handle_new_connection(listenfd, server_info.clients);
            }
            // Check activities from connected sockets
            ITER_LOOP(it, server_info.clients)
            {
                client_t* cli = (client_t *) iter_get_item(it);
                Node* cli_node = iter_get_node(it);
                if (FD_ISSET(cli->sock, &fds))
                {
                    DEBUG_PRINTF(DEBUG_CLIENTS, "Active fd=%i\n", cli->sock);
                    __rc = handle_data(&server_info, cli, cli_node);
                    // If something went wrong, fake a QUIT command
                    if (__rc < 0)
                    {
                        cli->registered = 1; // Ugly but quick fix
                        handle_line("QUIT", &server_info, cli, cli_node);
                    }
                }
            } /* Iterator loop */
            iter_clean(it);
        }
    }
    close(listenfd);
    
    return 0;
}



/* Build fd_set in |fds| given the listening socket |listenfd| and
 * client sockets |clients| array.
 */
int build_fd_set(fd_set *fds, int listenfd, LinkedList* clients)
{
    int highfd = listenfd;
    FD_ZERO(fds);
    FD_SET(listenfd, fds);
    // update highfd
    ITER_LOOP(it, clients)
    {
        client_t* cli = (client_t *) iter_get_item(it);
        int fd = cli->sock;
        FD_SET(fd, fds); // Register this socket
        if (fd > highfd) // Update |highfd| if necessary
            highfd = fd;
    }
    iter_clean(it);
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
int handle_new_connection(int listenfd, LinkedList* clients)
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
    if (clients->size == MAX_CLIENTS)
    {
        DEBUG_PRINTF(DEBUG_SOCKETS, "No room for new connections\n");
        close(sock);
        return -1;
    }
    
    // Ready to record client information
    client_t* cli = (client_t *) malloc(sizeof(client_t));
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
    strncpy(cli->hostname, host_buf, MIN( strlen(host_buf),
                                          MAX_HOSTNAME-1 ));  // -1 for the last '\0'
    
    // Initialize various fields
    memcpy(&(cli->cliaddr), &cli_addr, sizeof(cli_addr));
    cli->inbuf_size = 0;
    
    DEBUG_PRINTF(DEBUG_CLIENTS, "New client from %s, fd=%i\n",
            cli->hostname,
            cli->sock);
    
    add_item(clients, cli);
    return 0;
}




/* Handle new input data from the client.
 */
int handle_data(server_info_t* server_info, client_t* cli, Node* cli_node)
{
    // Precondition:
    // client's input buffer must contain less than RFC_MAX_MSG_LEN bytes
    assert(cli->inbuf_size < RFC_MAX_MSG_LEN);
    
    // Compute buffer offset
    char *buf_contd = cli->inbuf + cli->inbuf_size;
    long bytes_read = read(cli->sock,
                           buf_contd,
                           // Ensures message size <= RFC_MAX_MSG_LEN
                           RFC_MAX_MSG_LEN - cli->inbuf_size);
    
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
        DEBUG_PRINTF(DEBUG_INPUT, "EOF\n");
        return -1;
    }
    
    // Else, we've read some data
    DEBUG_PRINTF(DEBUG_INPUT, "<< Got:\n%s\n", buf_contd);
    
    char *msg = cli->inbuf; // Start of the msg
    char *cr, *lf, *end;
    while (msg < cli->inbuf + RFC_MAX_MSG_LEN)
    {
        // Look for the next '\r' or '\n'
        cr = strchr(msg, '\r');
        lf = strchr(msg, '\n');
        if (!cr && !lf) // All complete messages have been handled
            break;
        if (!cr)
            end = lf;
        else if (!lf)
            end = cr;
        else
            end = MIN(cr, lf);
        *end = '\0';
        
        DEBUG_PRINTF(DEBUG_INPUT, "Msg: %s\n", msg);

        handle_line(msg, server_info, cli, cli_node);
        
        msg = end + 1;
    }
    size_t remaining_msg_len = strlen(msg);
    // Nothing else to read
    // Also throw away a segment if it is already too long
    if ( remaining_msg_len == 0 || remaining_msg_len >= RFC_MAX_MSG_LEN)
    {
        memset(&(cli->inbuf), '\0', sizeof(cli->inbuf));
        cli->inbuf_size = 0;
    }
    // What remains in the buffer is the initial segment of an incomplete msg,
    // assuming the rest will be delivered next time.
    // Move this segment to the beginning of the buffer
    else
    {
        // Temporary buffer
        // Techically we only need RFC_MAX_MSG_LEN bytes, since remaining_msg_len < RFC_MAX_MSG_LEN
        char tmp[RFC_MAX_MSG_LEN+1];
        tmp[RFC_MAX_MSG_LEN] = '\0';
        // Copy msg -> tmp
        strcpy(tmp, msg);
        // Clear input buffer
        memset(&(cli->inbuf), '\0', sizeof(cli->inbuf));
        // Copy tmp -> msg
        strcpy(cli->inbuf, tmp);
        cli->inbuf_size = remaining_msg_len;
        
        DEBUG_PRINTF(DEBUG_INPUT, "Incomplete msg:\n%s\n", cli->inbuf);
//        print_hex(DEBUG_INPUT, cli->inbuf, MAX_MSG_LEN);
//        DEBUG_PRINTF(DEBUG_INPUT, "\n");
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
