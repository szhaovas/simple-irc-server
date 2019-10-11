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
#include "linked-list.h"

#define MAX_COMMAND 16

/**
 * You'll want to define the CMD_ARGS to match up with how you
 * keep track of clients.  Probably add a few args...
 * The command handler functions will look like
 * void cmd_nick(CMD_ARGS)
 * e.g., void cmd_nick(your_client_thingy *c, char *prefix, ...)
 * or however you set it up.
 */
#define CMD_ARGS server_info_t* server_info, client_t* cli, char **params, int nparams
typedef void (*cmd_handler_t)(CMD_ARGS);
#define COMMAND(cmd_name) void cmd_name(CMD_ARGS)

// Server reply macro
#define WRITE(sock, fmt, ...) do { dprintf(sock, fmt, ##__VA_ARGS__); } while (0)

// Message of the day
#define MOTD_STR "ようこそ、OZの世界へ"

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

    // Target name in replies
    char* target = *cli->nick ? cli->nick : "*";

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
        // Send an unknown command error
        WRITE(cli->sock,
              ":%s %d %s %s :Unknown command\r\n",
              server_info->hostname,
              ERR_NEEDMOREPARAMS,
              target,
              command);
        return;
    }

    while (*command == ' '){
        *command++ = 0;
    }

    if (*command == '\0'){
        // Send an unknown command error
        WRITE(cli->sock,
              ":%s %d %s %s :Unknown command\r\n",
              server_info->hostname,
              ERR_NEEDMOREPARAMS,
              target,
              command);
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

    // Ignore a command if provided with a prefix different from the client's nickname
    if (prefix && *cli->nick && !strcmp(prefix, cli->nick))
        return;

    for (i = 0; i < NELMS(cmds); i++)
    {
        // Specified command matches with a command (case-insensitive)
        if (!strcasecmp(cmds[i].cmd, command))
        {
            // ERROR - the client needs to be in order to use this command
            if (cmds[i].needreg && !cli->registered)
            {
                WRITE(cli->sock,
                      ":%s %d %s :You have not registered\r\n",
                      server_info->hostname,
                      ERR_NOTREGISTERED,
                      target);
            }
            else if (nparams < cmds[i].minparams)
            {
                //There are commands that need to return erros more specific than ERR_NEEDMOREPARAMS
                //NICK: ERR_NONICKNAMEGIVEN
                //PRIVMSG: ERR_NORECIPIENT, ERR_NOTEXTTOSEND
                if (!strcasecmp(command, "NICK") || !strcasecmp(command, "PRIVMSG")) {
                    //ignore minparams restriction and pass down to command handlers
                    (*cmds[i].handler)(server_info, cli, params, nparams);
                }

                // ERROR - the client didn't specify enough parameters for this command
                WRITE(cli->sock,
                      ":%s %d %s %s :Not enough parameters\r\n",
                      server_info->hostname,
                      ERR_NEEDMOREPARAMS,
                      target,
                      command);
            }
            else // Call cmd_foo handler.
            {
                (*cmds[i].handler)(server_info, cli, params, nparams);
            }
            return; /* Command already processed */
        }
    }

    if (i == NELMS(cmds)){
        // ERROR - unknown command
        WRITE(cli->sock,
              ":%s %d %s %s :Unknown command\r\n",
              server_info->hostname,
              ERR_UNKNOWNCOMMAND,
              target,
              command);
        return;
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
int check_collision(size_t this_len, char* this, char* that)
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
    // Implies two strings have the same length and
    // equivalent sequence of characters
    return 1;
}


/**
 * Send MOTD messages.
 */
void motd(int sock, char* hostname)
{
    WRITE(sock,
          ":%s %d :- %s Message of the day - \r\n",
          hostname,
          RPL_MOTDSTART,
          hostname);

    WRITE(sock,
          ":%s %d :- %s\r\n",
          hostname,
          RPL_MOTD,
          MOTD_STR);

    WRITE(sock,
          ":%s %d :End of /MOTD command\r\n",
          hostname,
          RPL_ENDOFMOTD);
}



/* Command handlers */

/**
 * Command NICK
 */
void cmdNick(CMD_ARGS)
{
    //if nparmas = 0, reply ERR_NONICKNAMEGIVEN
    if (!nparams) {
        WRITE(cli->sock,
              ":%s %d %s :No nickname given\r\n",
              server_info->hostname,
              ERR_NONICKNAMEGIVEN,
              *cli->nick? cli->nick: "*");
        return;
    }

    char* nick = params[0];
    char nick_buf[RFC_MAX_NICKNAME+1];
    nick_buf[RFC_MAX_NICKNAME] = '\0';
    strncpy(nick_buf, nick, RFC_MAX_NICKNAME);
    size_t nick_len = strlen(nick_buf);
    int nick_valid = is_nickname_valid(nick, nick_len);

    if (!nick_valid)
    {
        WRITE(cli->sock,
              ":%s %d %s %s :Erroneus nickname\r\n",
              server_info->hostname,
              ERR_ERRONEOUSNICKNAME,
              *cli->nick? cli->nick: "*",
              nick_buf);
    }
    else /* nick valid */
    {
        // Check for nickname collision
        Iterator_LinkedList* it;
        for (it = iter(server_info->clients); !iter_empty(it); it = iter_next(it))
        {
            client_t* other = (client_t *) iter_get(it);
            if (cli == other) continue;
            if (*other->nick && // Note: we should not check |registered| here,
                                // because two unregistered clients may still have colliding nicknames
                check_collision(nick_len, nick_buf, other->nick))
            {
                WRITE(cli->sock,
                      ":%s %d %s %s :Nickname is already in use\r\n",
                      server_info->hostname,
                      ERR_NICKNAMEINUSE,
                      *cli->nick? cli->nick: "*",
                      nick_buf);
                return iter_clean(it);
            }
        } /* Iterator loop */
        iter_clean(it);

        /* No collision */

        // Make a copy of old nickname, if any
        char old_nick[RFC_MAX_NICKNAME];
        if (*cli->nick)
            strcpy(old_nick, cli->nick);

        // Set client's nickname
        strcpy(cli->nick, nick_buf); // Edge case: new nick same as old nick => No effect

        // If user already is in a channel
        // => Echo NICK to everyone else in the same channel
        if (cli->channel)
        {
            Iterator_LinkedList* it;
            for (it = iter(cli->channel->members); !iter_empty(it); it = iter_next(it))
            {
                client_t* other = (client_t *) iter_get(it);
                if (cli == other) continue;
                WRITE(other->sock,
                      ":%s!%s@%s NICK %s\r\n",
                      old_nick,
                      cli->user,
                      cli->hostname,
                      cli->nick);
            } /* Iterator loop */
            return iter_clean(it);
        }

        // Otherwise, the client is not in any channel, so she is either
        // (a) registered, or (b) not registered, in which case we attempt to
        // complete the registration if possible
        if (!cli->registered && *cli->user)
        {
            cli->registered = 1;
            motd(cli->sock, server_info->hostname);
        }
    }
}



void cmdUser(CMD_ARGS){
    // ERROR - already registered
    if (cli->registered)
    {
        WRITE(cli->sock,
              ":%s %d %s :You may not reregister\r\n",
              server_info->hostname,
              ERR_ALREADYREGISTRED,
              *cli->nick? cli->nick: "*");
    }

    // Update user information
    strncpy(cli->user, params[0], MAX_USERNAME-1);
    strncpy(cli->servername, params[2], MAX_SERVERNAME-1);
    strncpy(cli->realname, params[3], MAX_REALNAME-1);
    // Edge case -
    // If the client is not registered but already has already issued USER, i.e.,
    // she hasn't run NICK but has run USER, then existing user infomation is
    // silently overwritten

    // Register the client if possible
    if (!cli->registered && *cli->nick)
    {
        cli->registered = 1;
        motd(cli->sock, server_info->hostname);
    }
}



void cmdQuit(CMD_ARGS)
{
    // Close the connection
    close(cli->sock);

    // Remove client from the server's client list
    // (Junrui) FIXME: This iterates over the whole list and defeats the purpose?
    drop_item(server_info->clients, cli);

    // If the client has joined a channel, then we need to
    //   1. Remove her from the member list
    //   2. Echo QUIT to other members, if any
    //   3. If the departing client is the last one in the channel,
    //      then the channel is removed
    if (cli->channel)
    {
        // Loop through members from the client's channel
        Iterator_LinkedList* it;
        for (it = iter(cli->channel->members);
             !iter_empty(it);
             it = iter_next(it))
        {
            client_t* other = (client_t *) iter_get(it);

            // Client herself => Remove client from the channel member list
            if (cli == other)
                iter_drop(it);

            // Someone else => Echo quit message
            else
            {
                // Client did not specify the farewell message => Use default
                if (!nparams)
                {
                    WRITE(other->sock,
                          ":%s!%s@%s QUIT :Connection closed\r\n",
                          cli->nick,
                          cli->user,
                          cli->hostname);
                }
                // Client has specified farewell message
                else
                {
                    WRITE(cli->sock,
                          ":%s!%s@%s QUIT :%s\r\n",
                          cli->nick,
                          cli->user,
                          cli->hostname,
                          params[0]);
                }
            }
        } /* Iterator loop */
        iter_clean(it);

        if (cli->channel->members->size == 0)
        {
            drop_item(server_info->channels, cli->channel);
        }
        // FIXME: free channel struct
        // FIXME: free client struct
    }
}



void cmdJoin(CMD_ARGS)
{
    /* do something */

}

void cmdPart(CMD_ARGS)
{
    /* do something */

}

void cmdList(CMD_ARGS)
{
    /* do something */

}

void cmdPmsg(CMD_ARGS)
{
    //server_info_t* server_info, client_t* cli, char **params, int nparams
    //<target>{,<target>} <text to be sent>
    //ERR_NORECIPIENT
    //ERR_NOTEXTTOSEND
    //ERR_NOSUCHNICK (when cannot find nick/channame?)

    //Since there is no way to tell between target and text_to_send
    //if nparams == 0 then reply ERR_NORECIPIENT
    //if nparams == 1 then reply ERR_NOTEXTTOSEND
    if (!nparams) {
        WRITE(cli->sock,
              ":%s %d %s :No recipient given (PRIVMSG)\r\n",
              server_info->hostname,
              ERR_NORECIPIENT,
              cli->nick);
        return;
    } else if (nparams == 1) {
        WRITE(cli->sock,
              ":%s %d %s :No text to send\r\n",
              server_info->hostname,
              ERR_NOTEXTTOSEND,
              cli->nick);
        return;
    }

    //parsing targets by ","
  	char *target = strtok(strdup(params[0]), ",");
    while(target) {

      //is target a client?
      Iterator_LinkedList* c;
      for (c = iter(server_info->clients); !iter_empty(c); c = iter_next(c)) {

          client_t* sendTo = (client_t *) iter_get(c);
          if (strcmp(target, sendTo->nick)) {
              //yes, target is a client
              //send to the client
              WRITE(sendTo->sock,
                    ":%s!%s@%s :%s\r\n",
                    sendTo->nick,
                    sendTo->user,
                    sendTo->hostname,
                    params[1]);
              break;
          }
      }
      iter_clean(c);

      //is target a channel?
      Iterator_LinkedList* ch;
      for (ch = iter(server_info->channels); !iter_empty(ch); ch = iter_next(ch)) {

          channel_t* sendTo = (channel_t *) iter_get(ch);
          if (strcmp(target, sendTo->name)) {
              //yes, target is a channel
              //send to every member except the sender client
              Iterator_LinkedList* m;
              for (m = iter(sendTo->members); !iter_empty(m); m = iter_next(m)) {

                  client_t* other = (client_t *) iter_get(m);
                  if (cli == other) {
                      continue;
                  } else {
                      WRITE(other->sock,
                            ":%s!%s@%s :%s\r\n",
                            other->nick,
                            other->user,
                            other->hostname,
                            params[1]);
                  }
              }
              iter_clean(m);
              break;
          }
      }
      iter_clean(ch);

      //this target is neither a client nor a channel, return ERR_NOSUCHNICK
      //FIX: print two nicks? ---------------------------------------------------------------------------------------------------------------------------------------
      WRITE(cli->sock,
            ":%s %d %s %s :No such nick/channel\r\n",
            server_info->hostname,
            ERR_NOSUCHNICK,
            cli->nick,
            cli->nick);

      //go to next target
      target = strtok(NULL, ",");
    }
}

void cmdWho(CMD_ARGS)
{
    /* do something */

}
