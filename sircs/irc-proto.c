#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h> // isalpha(), isdigit()
#include <stdarg.h> // va_list, etc.

#include "irc-proto.h"
#include "sircs.h"
#include "debug.h"

#define MAX_COMMAND 16

/**
 * You'll want to define the CMD_ARGS to match up with how you
 * keep track of clients.  Probably add a few args...
 * The command handler functions will look like
 * void cmd_nick(CMD_ARGS)
 * e.g., void cmd_nick(your_client_thingy *c, char *prefix, ...)
 * or however you set it up.
 */
#define CMD_ARGS server_info_t* server_info, client_t* cli, char *prefix, char **params, int nparams
typedef int (*cmd_handler_t)(CMD_ARGS);
#define COMMAND(cmd_name) int cmd_name(CMD_ARGS)

struct dispatch {
    char cmd[MAX_COMMAND];
    int needreg; /* Must the user be registered to issue this cmd? */
    int minparams; /* send NEEDMOREPARAMS if < this many params */
    cmd_handler_t handler;
};

#define NELMS(array) (sizeof(array) / sizeof(array[0]))

/* Define the command handlers here.  This is just a quick macro
 * to make it easy to set things up */
COMMAND(cmdNick);
COMMAND(cmdUser);
COMMAND(cmdQuit);
COMMAND(cmdJoin);
COMMAND(cmdPart);
COMMAND(cmdList);
COMMAND(cmdPmsg);
COMMAND(cmdWho);

/**
 * Dispatch table.  "reg" means "user must be registered in order
 * to call this function".  "#param" is the # of parameters that
 * the command requires.  It may take more optional parameters.
 */
 struct dispatch cmds[] = {/* cmd,    reg  #parm  function usage*/
                           { "NICK",    0, 1, cmdNick},
                           { "USER",    0, 4, cmdUser},
                           { "QUIT",    1, 0, cmdQuit},
                           { "JOIN",    1, 1, cmdJoin},
                           { "PART",    1, 1, cmdPart},
                           { "LIST",    1, 0, cmdList},
                           { "PRIVMSG", 1, 2, cmdPmsg},
                           { "WHO",     1, 0, cmdWho},
                          };

/**
 Helper functions.
 */
int isspecial_(char c);

/**
 * Handle a command line.  NOTE:  You will probably want to
 * modify the way this function is called to pass in a client
 * pointer or a table pointer or something of that nature
 * so you know who to dispatch on...
 * Mostly, this is here to do the parsing and dispatching for you.
 *
 * This function takes a single line of text.  You MUST have
 * ensured that it's a complete line (i.e., don't just pass
 * it the result of calling read()).
 * Strip the trailing newline off before calling this function.
 */
void handleLine(char* line, server_info_t* server_info, client_t* cli)
{
    // Empty messages are silently iginored (as per RFC)
    if (*line == '\0') return;

    char *prefix = NULL, *command, *pstart, *params[MAX_MSG_TOKENS];
    int nparams = 0;
    char *trailing = NULL;

    DPRINTF(DEBUG_INPUT, "Handling line: %s\n", line);
    command = line;

    if (*line == ':'){
        prefix = ++line;
        command = strchr(prefix, ' ');
    }

    if (!command || *command == '\0'){
        /* Send an unknown command error! */
        char err_msg[RFC_MAX_MSG_LEN];
        sprintf(err_msg, ":%s %d %s %s :Unknown command\r\n", server_info->hostname, ERR_NEEDMOREPARAMS, cli->nick, command);
        write(cli->sock, err_msg, strlen(err_msg)+1);
        return;
    }

    while (*command == ' '){
        *command++ = 0;
    }

    if (*command == '\0'){
        char err_msg[RFC_MAX_MSG_LEN];
        sprintf(err_msg, ":%s %d %s %s :Unknown command\r\n", server_info->hostname, ERR_NEEDMOREPARAMS, cli->nick, command);
        write(cli->sock, err_msg, strlen(err_msg)+1);
        /* Send an unknown command error! */
        return;
    }

    pstart = strchr(command, ' ');

    if (pstart){
        while (*pstart == ' ')
            *pstart++ = '\0';

        if (*pstart == ':'){
            trailing = pstart;
        } else{
            trailing = strstr(pstart, " :");
        }

        if (trailing){
            while (*trailing == ' ')
                *trailing++ = 0;

            if (*trailing == ':')
                *trailing++ = 0;
        }

        do{
            if (*pstart != '\0'){
                params[nparams++] = pstart;
            } else{
                break;
            }
            pstart = strchr(pstart, ' ');

            if (pstart){
                while (*pstart == ' ')
                    *pstart++ = '\0';
            }
        } while (pstart != NULL && nparams < MAX_MSG_TOKENS);
    }

    if (trailing && nparams < MAX_MSG_TOKENS){
        params[nparams++] = trailing;
    }

    DPRINTF(DEBUG_INPUT, "Prefix:  %s\nCommand: %s\nParams (%d):\n",
            prefix ? prefix : "<none>", command, nparams);

    int i;
    for (i = 0; i < nparams; i++){
        DPRINTF(DEBUG_INPUT, "   %s\n", params[i]);
    }

    DPRINTF(DEBUG_INPUT, "\n");

    for (i = 0; i < NELMS(cmds); i++){
        if (!strcasecmp(cmds[i].cmd, command)){
            if (cmds[i].needreg && !cli->registered){
                char err_msg[RFC_MAX_MSG_LEN];
                sprintf(err_msg, ":%s %d %s :You have not registered\r\n", server_info->hostname, ERR_NOTREGISTERED, cli->nick);
                write(cli->sock, err_msg, strlen(err_msg)+1);
                /* ERROR - the client is not registered and they need
                 * to be in order to use this command! */
            } else if (nparams < cmds[i].minparams){
                /* ERROR - the client didn't specify enough parameters
                 * for this command! */
                 char err_msg[RFC_MAX_MSG_LEN];
                 sprintf(err_msg, ":%s %d %s %s :Not enough parameters\r\n", server_info->hostname, ERR_NEEDMOREPARAMS, cli->nick, cmds[i].cmd);
                 write(cli->sock, err_msg, strlen(err_msg)+1);
            } else {
                /* Here's the call to the cmd_foo handler... modify
                 * to send it the right params per your program
                 * structure. */
                // FIXME: check prefix match here
                (*cmds[i].handler)(server_info, cli, prefix, params, nparams);
            }
            break;
        }
    }

    if (i == NELMS(cmds)){
        /* ERROR - unknown command! */
        char err_msg[RFC_MAX_MSG_LEN];
        sprintf(err_msg, ":%s %d %s %s :Unknown command\r\n", server_info->hostname, ERR_NEEDMOREPARAMS, cli->nick, cmds[i].cmd);
        write(cli->sock, err_msg, strlen(err_msg)+1);
    }
}


/**
 * Check if character |c| is a special character.
 *
 * RFC Sec 2.3.1:
 *   <special> ::=
 *     '-'  |  '['  |  ']'  |  '\'  |  '`'  |  '^'  |  '{'  |  '}'
 */
int isspecial_(char c)
{
    return strchr("-[]|\\`^{}", c) != NULL;
}


/**
 * Generate a reply to the client
 * Arguments:
 *   n       - number of args exluding |n|
 *   1st arg - server info
 *   2nd arg - client's socket
 *   3rd arg - numerical reply (RPL or ERR)
 *   4th+    - string segments
 */
int reply(int n, ...)
{
    // Variable-length argument list
    va_list valist;
    va_start(valist, n); // Initialize valist for |n| number of arguments

    // 1st arg is server info
    server_info_t* server_info = va_arg(valist, server_info_t*);
    char snd_buf[RFC_MAX_MSG_LEN+1];
    char* ptr = snd_buf;

    // Prefix the msg with the originator, i.e. server hostname
    *ptr++ = ':';
    ptr = stpcpy(ptr, server_info->hostname); // Msg originates from server
    *ptr++ = ' ';

    // 2nd arg is the socket number
    int sock = va_arg(valist, int);

    // 3rd arg is the (integer) numeric reply
    snprintf(ptr, 4, "%i", va_arg(valist, int)); // int to str
    ptr += 3;

    // String segments
    for (int i = 0; i < n - 3; i++)
    {
        *ptr++ = ' ';
        char* seg = va_arg(valist, char*);
        ptr = stpcpy(ptr, seg);
    }

    ptr = stpcpy(ptr, "\r\n");

    // Clean memory reserved for valist
    va_end(valist);

    // Write reply message onto the socket
    if (write(sock, snd_buf, ptr - snd_buf + 1) < 0)
    {
        DEBUG_PERROR("write() failed");
        return -1;
    }

    return 0;
}



/**
 * Echo a command from a client to another client
 * Arguments:
 *   n     - number of args exluding |n|
 *   1     - server info
 *   2     - receiver's socket
 *   3     - originator client's user@host string
 *   4+    - string segments
 */
int echo_cmd(int n, ...)
{
    // Variable-length argument list
    va_list valist;
    va_start(valist, n); // Initialize valist for |n| number of arguments

    // Arg 1 is server info
    server_info_t* server_info = va_arg(valist, server_info_t*);
    char snd_buf[RFC_MAX_MSG_LEN+1];
    char* ptr = snd_buf;

    // Arg 2 is the socket number
    int sock = va_arg(valist, int);

    // Arg 3 is originator's user@host string
    *ptr++ = ':';
    char* orig = va_arg(valist, char*);
    ptr = stpcpy(ptr, orig);

    // String segments
    for (int i = 0; i < n - 3; i++)
    {
        *ptr++ = ' ';
        char* seg = va_arg(valist, char*);
        ptr = stpcpy(ptr, seg);
    }

    ptr = stpcpy(ptr, "\r\n");

    // Clean memory reserved for valist
    va_end(valist);

    // Write reply message onto the socket
    if (write(sock, snd_buf, ptr - snd_buf + 1) < 0)
    {
        DEBUG_PERROR("write() failed");
        // FIXME: do we need to remove client here?
        // FIXME: what about EAGAIN or EWOULDBLOCK?
        //   The file descriptor is for a socket, is marked O_NONBLOCK, and write would
        //   block.  The exact error code depends on the protocol, but EWOULDBLOCK is more
        //   common.
        return -1;
    }

    return 0;
}



/**
 * Check if a nickname is valid
 *
 * From RFC:
 *   <nick> ::= <letter> { <letter> | <number> | <special> }
 */
int is_nickname_valid(char* nick, size_t nick_len)
{
    for (int i = 0; i < nick_len; i++)
    {
        char c = nick[i];
        if ( (i == 0 && !isalpha(c)) ||
             (i > 0  && !(isalpha(c) || isdigit(c) || isspecial_(c))))
        {
            return FALSE;
        }
    }
    return TRUE;
}




/**
 * Check if two chars are equivalent
 *
 * From RFC:
 *   Because of IRC's scandanavian origin, the characters {}| are considered to be
 *   the lower case equivalents of the characters []\, respectively.
 *   This is a critical issue when determining the equivalence of two nicknames.
 */
int equivalent_char(char a, char b)
{
    if (a == b) return TRUE;
    switch (a)
    {
        case '{': return b == '[';
        case '}': return b == ']';
        case '[': return b == '{';
        case ']': return b == '}';
        case '|': return b == '\\';
        case '\\': return b == '|';
        default: return FALSE;
    }
}



/**
 * Check if two nicknames |this| and |that| collide.
 */
int check_colision(size_t this_len, char* this, char* that)
{
    for (int i = 0; i < this_len; i++)
    {
        char ai = this[i], bi = that[i];
        // Same length => same string
        if ( ai == '\0' && bi == '\0')
            return 1;
        // Different length
        else if ( (ai == '\0') ^ (bi == '\0') )
            return 0;
        // Non-equivalent chars
        else if ( !equivalent_char(ai, bi) )
            return 0;
    }
    return 1;
}



/* Command handlers */
/* MODIFY to take the arguments you specified above! */
/**
 * Command NICK
 */
int cmdNick(CMD_ARGS)
{
    char* nick = params[0];
    char nick_buf[RFC_MAX_NICKNAME+1];
    nick_buf[RFC_MAX_NICKNAME] = '\0';
    strncpy(nick_buf, nick, RFC_MAX_NICKNAME);
    size_t nick_len = strlen(nick_buf);
    int nick_valid = is_nickname_valid(nick, nick_len);
    if (nick_valid)
    {
        // Check for collision
        for (Iterator_LinkedList* it = iter(server_info->clients);
             !iter_empty(it);
             it = iter_next(it))
        {
            client_t* other = (client_t *) iter_yield(it);
            if (cli == other) continue;
            if (other &&
                *other->nick && // Note: we should not check |registered| here
                check_colision(nick_len, nick_buf, other->nick))
            {
                return reply(5, server_info, cli->sock, ERR_NICKNAMEINUSE, nick_buf, ":Nickname is already in use");
            }
        }
        // No collision
        strcpy(cli->nick, nick_buf); // Edge case: new nick == old nick => no effect & no reply

        // User already has a nickname (updates old nickname) & is registered
        // => echo NICK to every other registered clients in the same channel
        if (cli->registered)
        {
            for (Iterator_LinkedList* it = iter(server_info->clients);
                 !iter_empty(it);
                 it = iter_next(it))
            {
                client_t* other = (client_t *) iter_yield(it);
                if (cli == other) continue;
                if (other && other->registered && other->channel == cli->channel)
                {
                    char originator_buf[RFC_MAX_NICKNAME + MAX_USERNAME + MAX_HOSTNAME + 2];
                    sprintf(originator_buf, "%s!%s@%s", cli->nick, cli->user, cli->hostname);
                    return echo_cmd(5, server_info, other->sock, originator_buf, "NICK", cli->nick);
                }
            }
        }
        // Else, setting nickname for the first time,
        // or updating nickname for an unregistered user
        // => Do nothing
    }
    // Invalid nick
    else
    {
        return reply(4, cli->sock, ERR_ERRONEOUSNICKNAME, nick_buf, ":Erroneus nickname");
    }
    return 0; // No reply
}

int cmdUser(CMD_ARGS){
    // checking prefix
    // ONLY execute the command either when no prefix is provided or when the
    // provided prefix matches the client's username
    // otherwise silently ignore the command
    if (!prefix || !strcmp(prefix, cli->nick)) {
        // send error message if already registered
        if (cli->registered) {
            char err_msg[RFC_MAX_MSG_LEN];
            sprintf(err_msg, ":%s %d %s :You may not reregister\r\n",
                    server_info->hostname,
                    ERR_ALREADYREGISTRED,
                    cli->nick);
            return write(cli->sock, err_msg, strlen(err_msg)+1);
        }

        // otherwise store user information
        // if the client is not registered but already has a set of user information (e.g. hasn't run NICK but has run USER),
        //     we allow the existing user infomation to be overwritten
        strcpy(cli->user, params[0]);
        strcpy(cli->servername, params[2]);
        strcpy(cli->realname, params[3]);
        // and check if the client becomes registered
        // fix /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        if (cli->nick[0]) {
            cli->registered = 1;
        }
    }

    // prefix mismatch is not counted as error
    return 0;
}

int cmdQuit(CMD_ARGS)
{

    // checking prefix
    // ONLY execute the command either when no prefix is provided or when the
    // provided prefix matches the client's username
    // otherwise silently ignore the command
    if (!prefix || !strcmp(prefix, cli->nick)) {
        // close the connection
        close(cli->sock);

        char quit_msg[RFC_MAX_MSG_LEN];
        // echo quit message to all users on the same channel
        // if given quit message
        if (!nparams) {
            for (Iterator_LinkedList* it = iter(server_info->clients);
                 !iter_empty(it);
                 it = iter_next(it))
            {
                client_t* other = (client_t *) iter_yield(it);
                if (cli == other) continue;
                if (other && other->registered && other->channel == cli->channel)
                {
                    sprintf(quit_msg, ":%s!%s@%s QUIT :%s", cli->nick, cli->user, cli->hostname, params[0]);
                    write(other->sock, quit_msg, strlen(quit_msg)+1);
                }
            }
        }
        // else default quit message
        else {
            for (Iterator_LinkedList* it = iter(server_info->clients);
                 !iter_empty(it);
                 it = iter_next(it))
            {
                client_t* other = (client_t *) iter_yield(it);
                if (cli == other) continue;
                if (other && other->registered &&
                    !strncmp(other->channel, cli->channel, RFC_MAX_NICKNAME))
                {
                    sprintf(quit_msg, ":%s!%s@%s QUIT :Connection closed", cli->nick, cli->user, cli->hostname);
                    write(other->sock, quit_msg, strlen(quit_msg)+1);
                }
            }
        }

        // delete client information
        // fix /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        cli = NULL;
    }
    // prefix mismatch is not counted as error
    return 0;
}

int cmdJoin(CMD_ARGS)
{
    /* do something */
    return 0;
}

int cmdPart(CMD_ARGS)
{
    /* do something */
    return 0;
}

int cmdList(CMD_ARGS)
{
    /* do something */
    return 0;
}

int cmdPmsg(CMD_ARGS)
{
    /* do something */
    return 0;
}

int cmdWho(CMD_ARGS)
{
    /* do something */
    return 0;
}
