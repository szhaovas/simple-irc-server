#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>

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
#define CMD_ARGS client *cli, char *prefix, char **params, int nparams
typedef void (*cmd_handler_t)(CMD_ARGS);
#define COMMAND(cmd_name) void cmd_name(CMD_ARGS)

struct dispatch {
    char cmd[MAX_COMMAND];
    int needreg; /* Must the user be registered to issue this cmd? */
    int minparams; /* send NEEDMOREPARAMS if < this many params */
    cmd_handler_t handler;
    char *usgae; /* Usage instructions */
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
                           { "NICK",    0, 1, cmdNickm,    "NICK <nickname>"},
                           { "USER",    0, 4, cmdUser,     "USER <username> <hostname> <servername> <realname>"},
                           { "QUIT",    1, 0, cmdQuit,     "QUIT [<Quit message>]"},
                           { "JOIN",    1, 1, cmdJoin,     "JOIN <channel>"},
                           { "PART",    1, 1, cmdPart,     "PART <channel>"},
                           { "LIST",    1, 0, cmdList,     "LIST"},
                           { "PRIVMSG", 1, 2, cmdPrivmsg,  "PRIVMSG <target> <text to be sent>"},
                           { "WHO",     1, 0, cmdWho,      "WHO [<name>]"},
                          };

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
void handleLine(char *line, client *cli){
    // Empty messages are silently iginored (RFC)
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
        char server_hostname[MAX_HOSTNAME];
        get_server_hostname(server_hostname);
        sprintf(err_msg, ":%s %d %s %s :Unknown command\r\n", server_hostname, ERR_NEEDMOREPARAMS, cli->nick, cmds[i].cmd);
        write(cli->sock, err_msg, strlen(err_msg)+1);
        return;
    }

    while (*command == ' '){
        *command++ = 0;
    }

    if (*command == '\0'){
        char err_msg[RFC_MAX_MSG_LEN];
        char server_hostname[MAX_HOSTNAME];
        get_server_hostname(server_hostname);
        sprintf(err_msg, ":%s %d %s %s :Unknown command\r\n", server_hostname, ERR_NEEDMOREPARAMS, cli->nick, cmds[i].cmd);
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
                char server_hostname[MAX_HOSTNAME];
                get_server_hostname(server_hostname);
                sprintf(err_msg, ":%s %d %s :You have not registered\r\n", server_hostname, ERR_NOTREGISTERED, cli->nick);
                write(cli->sock, err_msg, strlen(err_msg)+1);
                /* ERROR - the client is not registered and they need
                 * to be in order to use this command! */
            } else if (nparams < cmds[i].minparams){
                /* ERROR - the client didn't specify enough parameters
                 * for this command! */
                 char err_msg[RFC_MAX_MSG_LEN];
                 char server_hostname[MAX_HOSTNAME];
                 get_server_hostname(server_hostname);
                 sprintf(err_msg, ":%s %d %s %s :Not enough parameters\r\n", server_hostname, ERR_NEEDMOREPARAMS, cli->nick, cmds[i].cmd);
                 write(cli->sock, err_msg, strlen(err_msg)+1);
            } else {
                /* Here's the call to the cmd_foo handler... modify
                 * to send it the right params per your program
                 * structure. */
                (*cmds[i].handler)(cli, prefix, params, nparams);
            }
            break;
        }
    }

    if (i == NELMS(cmds)){
        /* ERROR - unknown command! */
        char err_msg[RFC_MAX_MSG_LEN];
        char server_hostname[MAX_HOSTNAME];
        get_server_hostname(server_hostname);
        sprintf(err_msg, ":%s %d %s %s :Unknown command\r\n", server_hostname, ERR_NEEDMOREPARAMS, cli->nick, cmds[i].cmd);
        write(cli->sock, err_msg, strlen(err_msg)+1);
    }
}

// cat all strings in strs together and return the result pointer
// remember to free result pointer after use
// e.g.
// char *strs[] = {"abc", "def"};
// char *longer = strcat_l(strs, 2);
// free(longer);
// *longer is now "abcdef"
static char *strcat_l(char *strs[], int size) {
    int i;
    int total_length = 0;
    for (i=0; i < size; i++) {
        total_length += strlen(strs[i]);
    }

    char *result = (char *) malloc(total_length+1);
    strcpy(result, strs[0]);
    for (i=1; i < size; i++) {
        strcat(result, strs[i]);
    }

    return result;
}


/* Command handlers */
/* MODIFY to take the arguments you specified above! */
int cmdNick(CMD_ARGS){
    /* do something */
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
            char server_hostname[MAX_HOSTNAME];
            get_server_hostname(server_hostname);
            sprintf(err_msg, ":%s %d %s :You may not reregister\r\n", server_hostname, ERR_ALREADYREGISTRED, cli->nick);
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
        if (!cli->nick) {
          cli->registered = 1;
        }
    }

    // prefix mismatch is not counted as error
    return 0;
}

int cmdQuit(CMD_ARGS){
    /* do something */
    return 0;
}

int cmdJoin(CMD_ARGS){
    /* do something */
    return 0;
}

int cmdPart(CMD_ARGS){
    /* do something */
    return 0;
}

int cmdList(CMD_ARGS){
    /* do something */
    return 0;
}

int cmdPmsg(CMD_ARGS){
    /* do something */
    return 0;
}

int cmdWho(CMD_ARGS){
    /* do something */
    return 0;
}
