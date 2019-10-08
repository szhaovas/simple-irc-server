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

#include "debug.h"
#include <sys/types.h>
#include <netinet/in.h>

#define MAX_CLIENTS 512
#define MAX_MSG_TOKENS 10
#define MAX_MSG_LEN 6 // test
#define MAX_MSG_LEN_RPC 512
#define MAX_USERNAME 32
#define MAX_HOSTNAME 64
#define MAX_SERVERNAME 64
#define MAX_REALNAME 192
#define MAX_CHANNAME 64

typedef struct {
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

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

int handle_data(client *cli, char snd_buf[]);
void set_buf(char* str, int buf_size);
void test_buf(char* str, char* snd_buf);
void test_buf2(char* pre, char* str, char* snd_buf);
void print_repr(char* str);

int test = 0;
client* c;

int main(int argc, char* argv[])
{
//    set_debug("all");
    
    char snd_buf[MAX_MSG_LEN+1];
    c = malloc(sizeof(client));
    
    // Test with empty buffer
    test_buf("", snd_buf); // none
    test_buf("\n", snd_buf); // none
    test_buf("\r", snd_buf);  // none
    test_buf("\n\r", snd_buf); // none
    test_buf("\r\n", snd_buf); // none
    test_buf("12345\n", snd_buf); // #1: "12345"
    test_buf("1\n2\n", snd_buf); // #1: "1", #2: "2"
    test_buf("1\n2\n3\n", snd_buf); // #1: "1", #2: "2", #3: "3"
    test_buf("12\n3\n4", snd_buf); // #1: "12", #2: "3", incomplete: "4"
    test_buf("12345", snd_buf); // incomplete: "12345"
    
    // Test with non-empty buffer
    test_buf2("1", "2\n", snd_buf);
    test_buf2("1", "234\n", snd_buf);
    test_buf2("1234", "5\n", snd_buf);
    test_buf2("1", "2\n3\n4", snd_buf);
    
}

void set_buf(char* str, int buf_size)
{
    memset(c, '\0', sizeof(*c));
    strcpy(c->inbuf, str);
    c->inbuf_size = buf_size;
}

void test_buf2(char* pre, char* str, char* snd_buf)
{
    int pre_len = (int) strlen(pre);
    printf("Test #%i: ", test);
    print_repr(str);
    printf("\n");
    set_buf(pre, pre_len);
    strcpy(c->inbuf + pre_len, str);
    handle_data(c, snd_buf);
    test++;
    printf("\n");
}

void test_buf(char* str, char* snd_buf)
{
    printf("Test #%i: ", test);
    print_repr(str);
    printf("\n");
    set_buf(str, 0);
    handle_data(c, snd_buf);
    test++;
    printf("\n");
}


int handle_data(client *cli, char snd_buf[])
{
//    // Make sure there's still space to read anything
//    // FIXME: if not, the msg in the buffer is too long, so discard
//    assert(cli->inbuf_size < MAX_MSG_LEN);
//    // Compute remaning space
//    int buf_remaining = MAX_MSG_LEN - cli->inbuf_size;
//    // Compute buffer offset
//    char *buf = cli->inbuf + cli->inbuf_size;
//    int bytes_read = (int) read(cli->sock, buf, buf_remaining);
//    if (bytes_read < 0)
//    {
//        // Nothing to read due to non-blocking fd
//        if (errno == EAGAIN)
//            return 0;
//        // Something else went wrong (connection reset by client, etc.)
//        else
//        {
//            DEBUG_PERROR("read() failed");
//            return -1;
//        }
//    }
//    // Connection closed by client (EOF)
//    else if (bytes_read == 0)
//    {
//        DPRINTF(DEBUG_INPUT, "EOF\n");
//        return -1;
//    }
//    // Print received contents
//    DPRINTF(DEBUG_INPUT, "<< Got:   %s\n", buf);
//    DPRINTF(DEBUG_INPUT, "<< Bytes: ");
//    print_hex(DEBUG_INPUT, buf, MAX_MSG_LEN+1);
//    DPRINTF(DEBUG_INPUT, "\n");
    
    char *msg = cli->inbuf; // start of a (potential) msg
    char *cr, *lf, *end;
    int count = 0;
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
        if (*msg != '\0'){
            DPRINTF(DEBUG_INPUT, "msg %i: %s", count, msg);
//        print_hex(DEBUG_INPUT, msg, MAX_MSG_LEN); // replace this with handleLine(msg, cli);
            DPRINTF(DEBUG_INPUT, "\n");
        }
        msg = end + 1;
        count += 1;
    }
    // Nothing else to read
    // FIXME: should we throw away a segment if it is already too long (>=512 bytes?)
    if ( *msg == '\0')
    {
        memset(&(cli->inbuf), '\0', MAX_MSG_LEN);
        cli->inbuf_size = 0; // FIXME: this variable is not being used?
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
        
        DPRINTF(DEBUG_INPUT, "Incomplete: %s\n", cli->inbuf);
//        print_hex(DEBUG_INPUT, msg, MAX_MSG_LEN);
        DPRINTF(DEBUG_INPUT, "\n");
        
    }
    
    // FIXME: what if msg is 1.5 times the size of the buffer (N)?
    // The first N bytes will be thrown away, but the next 0.5 times will fit
    // and be interpreted. Then we should throw an unknown cmd error.
    
    return 0;
}


void print_repr(char* str)
{
    size_t len = strlen(str);
    for (int i = 0; i < len; i++)
    {
        switch (str[i])
        {
            case '\r': printf("\\r"); break;
            case '\n': printf("\\n"); break;
            default: printf("%c", str[i]);
        };
    }
}
