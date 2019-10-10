#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h> // isalpha(), isdigit()
#include <stdarg.h> // va_list, etc.
#include <assert.h>

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
 * Check if a character is a special character.
 *
 * <special> ::=
 *   '-'  |  '['  |  ']'  |  '\'  |  '`'  |  '^'  |  '{'  |  '}'
 */
#define IS_SPECIAL(c) (strchr("-[]|\\`^{}", c) != NULL)


/**
 * Check if a character is a channel character.
 *
 * <chstring> ::=
 *   <any 8bit code except SPACE, BELL, NUL, CR, LF and comma (',')>
 */
#define IS_CH_CHAR(c) (strchr(" \a\r\n,", c) == NULL)



/**
 * Check if a nickname is valid
 *
 * <nick> ::= <letter> { <letter> | <number> | <special> }
 */
int is_nickname_valid(char* nick)
{
    if (*nick == '\0')
        return FALSE;
    for (int i = 0; i < RFC_MAX_NICKNAME + 1; i++)
    {
        if (nick[i] == '\0') return TRUE;
        if (i == RFC_MAX_NICKNAME) return FALSE;
        char c = nick[i];
        if ( (i == 0 && !isalpha(c)) ||
            (i > 0  && !(isalpha(c) || isdigit(c) || IS_SPECIAL(c))))
        {
            return FALSE;
        }
    }
    return TRUE;
}


/**
 * Check if a channel name is valid
 *
 * <channel> ::=
 * ('#' | '&') <chstring>
 */
int is_channel_valid(char* ch_name)
{
    if ( *ch_name != '#' || *ch_name != '&' )
        return FALSE;
    for (int i = 1; i < RFC_MAX_NICKNAME + 1; i++)
    {
        if (ch_name[i] != '\0') return TRUE;
        if (i == RFC_MAX_NICKNAME) return FALSE;
        if (!IS_CH_CHAR(ch_name[i])) return FALSE;
    }
    return TRUE; // Never reached
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
int check_collision(char* this, char* that)
{
    for (int i = 0; i < RFC_MAX_NICKNAME; i++)
    {
        char ai = this[i], bi = that[i];
        // Same length && equiv chars so far => same string
        if ( ai == '\0' && bi == '\0')
            return 1;
        // Different length
        else if ( (ai == '\0') ^ (bi == '\0') )
            return 0;
        // Non-equivalent chars
        else if ( !equivalent_char(ai, bi) )
            return 0;
    }
    // End of both strings => the same len and equiv chars
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


/**
 * Remove an empty channel.
 */
void remove_empty_channel(server_info_t* server_info, channel_t* ch)
{
    assert(ch->members->size == 0);
    drop_item(server_info->channels, ch);
    free(ch->members);
    free(ch);
}


/**
 * Build a multi-segment message that may exceed size limit. If so, the message
 * is split into multiple messages sharing a common prefix string, and they are
 * sent individually.
 *
 * The message must look like:
 *     <common> <segment> {<separator> <segment>} <CR-LF>
 */
void build_and_send_message(int   sock,
                            char* buf,       size_t buf_max,
                            char* common,    size_t common_len,
                            char* segment,   size_t segment_len,
                            char* separator)
{
    size_t buf_size = strlen(buf);
    // Buffer already contains some segments,
    if (buf_size > 0)
    {
        // Nothing more to build, so send everything we have
        if (!segment)
        {
            WRITE(sock, "%s\r\n", buf);
            return;
        }
        // Safe to append this new segment
        else if (buf_size + segment_len + 2 < buf_max) // 2 for "\r\n"
        {
            sprintf(buf + buf_size, "%s%s", separator, segment);
            return;
        }
        // Appending this new segment would exceed the limit
        // => send everything we have, and store the new segment on an empty buffer (fall through)
        else
        {
            WRITE(sock, "%s\r\n", buf);
        }
        
    }
    // Empty buffer
    memset(buf, '\0', buf_max);
    sprintf(buf, "%s%s", common, segment);
}


/* Command handlers */

/**
 * Command NICK
 */
void cmdNick(CMD_ARGS)
{
    int nick_valid = is_nickname_valid(params[0]);
    if (!nick_valid)
    {
        char nick_safe[RFC_MAX_NICKNAME+1];
        nick_safe[RFC_MAX_NICKNAME] = '\0';
        strncpy(nick_safe, params[0], RFC_MAX_NICKNAME);
        WRITE(cli->sock,
              ":%s %d %s %s :Erroneus nickname\r\n",
              server_info->hostname,
              ERR_ERRONEOUSNICKNAME,
              *cli->nick? cli->nick: "*",
              nick_safe);
    }
    else /* nick valid */
    {
        char* nick = params[0];
        // Check for nickname collision
        Iterator_LinkedList* it;
        for (it = iter(server_info->clients); !iter_empty(it); it = iter_next(it))
        {
            client_t* other = (client_t *) iter_get(it);
            if (cli == other) continue;
            if (*other->nick && // Note: we should not check |registered| here,
                // because two unregistered clients may still have colliding nicknames
                check_collision(nick, other->nick))
            {
                WRITE(cli->sock,
                      ":%s %d %s %s :Nickname is already in use\r\n",
                      server_info->hostname,
                      ERR_NICKNAMEINUSE,
                      *cli->nick? cli->nick: "*",
                      nick);
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
        strcpy(cli->nick, nick); // Edge case: new nick same as old nick => No effect
        
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
    } /* nick valid */
}



void cmdUser(CMD_ARGS){
    // ERROR - already registered
    if (cli->registered)
    {
        WRITE(cli->sock,
              ":%s %d %s :You may not reregister\r\n",
              server_info->hostname,
              ERR_ALREADYREGISTRED,
              cli->nick);
    }
    
    // Update user information
    strncpy(cli->user, params[0], MAX_USERNAME-1);
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
    // Backward pointer?
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
                // For safety reasons, ignore client's farewell message and always use default
                WRITE(other->sock,
                      ":%s!%s@%s QUIT :Connection closed\r\n",
                      cli->nick,
                      cli->user,
                      cli->hostname);
            }
        } /* Iterator loop */
        iter_clean(it);
        
        if (cli->channel->members == 0)
            remove_empty_channel(server_info, cli->channel);
    } /* if (cli->channel) */
    free(cli);
}


void cmdJoin(CMD_ARGS)
{
    // Junrui.1: you should verify that the channel name specified by the client is valid
    // Never, ever, trust a client. All clients are evil.
    // Just use is_channel_name_valid() which I have written
    
    // Junrui.2: the channel the client requests to join may be non-existent.
    // As per RFC, you should send an error reply
    
    
    if (strcmp(params[0], cli->channel->name)) // if user's channel same as param
        // Junrui.1: Should be !strcmp
        // Junrui.2: cli->channel may be NULL (client may not have joined a channel yet). This will cause seg fault.
        // Junrui.3: Use strncmp if you cann't guarantee the safety of the parameter strings (in this case the string was supplied by the client, hence unsafe)
        return; // ignore
    
    else if (cli->channel) // already has channel
    {
        //       cmdPart(CMD_ARGS);   // parts current channel
        // Junrui.1: this isn't quite how CMD_ARGS is supposed to used
        // Junrui.2: According to Rui's email, we might want to echo QUIT here, instaed of PART
        
        // check if channel exists
        for (Iterator_LinkedList* it = iter(server_info->channels);
             !iter_empty(it);
             it = iter_next(it))
        {
            channel_t* ch = (channel_t *) iter_get(it);
            if (strcmp(params[0], ch->name)) // channel found
                // Junrui: Should be !strcmp
            {
                add_item(ch->members, cli); // add cli to list of members in channel
                cli->channel = ch; // make client's channel
            }
        }
        // Junrui: please call iter_clean(it) after you're done
            
        //when channel doesn't exist
        // Junrui: we don't know if a channel exists or not at this point
        // If it exists, then the code below will also be executed, which is not what you want
        // Idea: put a flag to indicate if a channel match is found
        
        channel_t* chan = malloc(sizeof(channel_t)); // create new requested channel
        
        strcpy(chan->name, params[0]);
        
        // Junrui: you have not initialized the |member| linked list yet (simply do a malloc).
        add_item(chan->members, cli); // channels first member as the user
        add_item(server_info->channels, chan);    // add new channel to list of channels
        cli->channel = chan;              // client's channel as the channel created
        // Junrui: Bug: the client may be added twice. Try to figure out why.
        
    }
    // Junrui: we may want to handle the else branch here (client doesn't already have a channel)
    
    /* Several things that I see missing:
        1. If a client was the only member in his/her previous channel,
           then that channel should be removed.
        2. As per RFC, the JOIN should be echoed to all members of the newly joined channel
        3. As per RFC, the server should send the list of channel members to the client
     */
}


void cmdPart(CMD_ARGS)
{
    char quit_msg[RFC_MAX_MSG_LEN]; // create quit msg buf
    sprintf(quit_msg, ":%s!%s@%s QUIT",
            cli->nick, cli->user, cli->hostname); // send quit msg
    
    // remove client from channel members
    for (Iterator_LinkedList* it = iter(cli->channel->members);
         !iter_empty(it);
         it = iter_next(it))
    {
        client_t* other = (client_t *) iter_get(it);
        if (cli == other)
            iter_drop(it);
        else
            write(other->sock, quit_msg, strlen(quit_msg)+1); // quit msg to all other users
    }
}




void cmdPmsg(CMD_ARGS)
{
    /* do something */
    
}

void cmdWho(CMD_ARGS)
{
    /* do something */
    
}
