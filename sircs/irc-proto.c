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
struct dispatch cmds[] = {
    /* cmd,    reg  #parm  function */
    { "NICK",    0, 1, cmdNick }, // Basic
    { "USER",    0, 4, cmdUser },
    { "QUIT",    1, 0, cmdQuit },
    { "JOIN",    1, 1, cmdJoin }, // Channel
    { "PART",    1, 1, cmdPart },
    { "LIST",    1, 0, cmdList },
    { "PRIVMSG", 1, 1, cmdPmsg }, // Advanced
    { "WHO",     1, 0, cmdWho }
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
        //some_of_your_code_better_go_here();
        return;
    }
    
    while (*command == ' '){
        *command++ = 0;
    }
    
    if (*command == '\0'){
        // and_more_of_your_code_should_go_here();
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
                // youshouldputcodehere();
                /* ERROR - the client is not registered and they need
                 * to be in order to use this command! */
            } else if (nparams < cmds[i].minparams){
                /* ERROR - the client didn't specify enough parameters
                 * for this command! */
                // and_you_should_put_code_here_too();
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
        // yet_again_you_should_put_code_here();
    }
}


/* Command handlers */
/* MODIFY to take the arguments you specified above! */
void cmdNick(client *cli, char *prefix, char **params, int nparams){
    /* do something */
}

void cmdUser(client *cli, char *prefix, char **params, int nparams){
    /* do something */
}

void cmdQuit(client *cli, char *prefix, char **params, int nparams){
    /* do something */
}

void cmdJoin(client *cli, char *prefix, char **params, int nparams){
    /* do something */
}

void cmdPart(client *cli, char *prefix, char **params, int nparams){
    /* do something */
}

void cmdList(client *cli, char *prefix, char **params, int nparams){
    /* do something */
}

void cmdPmsg(client *cli, char *prefix, char **params, int nparams){
    /* do something */
}

void cmdWho(client *cli, char *prefix, char **params, int nparams){
    /* do something */
}
