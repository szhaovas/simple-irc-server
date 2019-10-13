#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h> // isalpha(), isdigit()
#include <stdarg.h> // va_list, etc.
#include <assert.h> // assert()

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
#define CMD_ARGS server_info_t* server_info, client_t* cli, Node* cli_node, char **params, int nparams
typedef void (*cmd_handler_t)(CMD_ARGS);
#define COMMAND(cmd_name) void cmd_name(CMD_ARGS)

// Server reply macro
#define GET_SAFE_NAME(safe_name, unsafe_name) \
char safe_name[RFC_MAX_NICKNAME+1]; \
safe_name[RFC_MAX_NICKNAME] = '\0'; \
strncpy(safe_name, unsafe_name, RFC_MAX_NICKNAME);

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
    { "NICK",    0, 0, cmdNick},
    { "USER",    0, 4, cmdUser},
    { "QUIT",    1, 0, cmdQuit},
    { "JOIN",    1, 1, cmdJoin},
    { "PART",    1, 1, cmdPart},
    { "LIST",    1, 0, cmdList},
    { "PRIVMSG", 1, 0, cmdPmsg},
    { "WHO",     1, 0, cmdWho},
};


/**
 * Send a reply.
 */

//#define unsafe_reply(sock, fmt, ...) do { dprintf(sock, fmt, ##__VA_ARGS__); } while (0)
//#define unsafe_vreply(sock, fmt, va) do { vdprintf(sock, fmt, va); } while (0)

void vreply(server_info_t* server_info,  client_t* cli, Node* cli_node,
            const char* restrict format, va_list args)
{
    if (!cli->zombie)
    {
        // Copy args for debug printing
        va_list args_copy;
        va_copy(args_copy, args);
        DEBUG_PRINTF(DEBUG_REPLIES, "+--------------------------------+\n");
        DEBUG_PRINTF(DEBUG_REPLIES, "|         To: (fd=%d) %9s   |\n", cli->sock, (*cli->nick)?cli->nick:"*");
        DEBUG_PRINTF(DEBUG_REPLIES, "+--------------------------------+\n");
        DEBUG_PRINTF(DEBUG_REPLIES, "| ");
        DEBUG_VPRINTF(DEBUG_REPLIES, format, args_copy);
        DEBUG_PRINTF(DEBUG_REPLIES, "|                                |\n");
        DEBUG_PRINTF(DEBUG_REPLIES, "+------------------------- End --+\n\n");
        DEBUG_PRINTF(DEBUG_REPLIES, "+------------------------- End --+\n\n");
        va_end(args_copy);
        
        // Write to client socket and check errors
        size_t num_bytes = vdprintf(cli->sock, format, args);
        assert(num_bytes <= RFC_MAX_MSG_LEN);
        if (num_bytes < 0)
        {
            // Mark client as zombie, and add to the list of zombies
            cli->zombie = TRUE;
            add_item(server_info->zombies, cli);
            // ECHO - QUIT
            cmdQuit(server_info, cli, cli_node, NULL, 0);
        }
    }
}

void reply(server_info_t* server_info, client_t* cli, Node* cli_node,
           const char* restrict format, ...)
{
    // Retrieve va_list and send reply message
    va_list args;
    va_start(args, format);
    vreply(server_info, cli, cli_node, format, args);
    va_end(args);
}


/**
 * Handle a command line.
 * Mostly, this is here to do the parsing and dispatching for you.
 *
 * This function takes a single line of text.  You MUST have
 * ensured that it's a complete line (i.e., don't just pass
 * it the result of calling read()).
 * Strip the trailing newline off before calling this function.
 */
void handle_line(char* line, server_info_t* server_info, client_t* cli, Node* cli_node)
{
    // Empty messages are silently iginored (as per RFC)
    if (*line == '\0') return;
    // Target name in replies
    char* target = *cli->nick ? cli->nick : "*";
    char *prefix = NULL, *command, *pstart, *params[MAX_MSG_TOKENS];
    int nparams = 0;
    char *trailing = NULL;
    DEBUG_PRINTF(DEBUG_INPUT, "Handling line: %s\n", line);
    command = line;
    if (*line == ':'){
        prefix = ++line;
        command = strchr(prefix, ' ');
    }
    if (!command || *command == '\0'){
        // ERROR - unknown command
        reply(server_info, cli, cli_node,
              ":%s %d %s * :Unknown command\r\n", // CHOICE: Cannot use |command| in this message
              server_info->hostname,
              ERR_NEEDMOREPARAMS,
              target);
        return;
    }
    while (*command == ' '){
        *command++ = 0;
    }
    if (*command == '\0'){
        // ERROR - unknown command
        reply(server_info, cli, cli_node,
              ":%s %d %s * :Unknown command\r\n", // CHOICE: Cannot use |command| in this message
              server_info->hostname,
              ERR_NEEDMOREPARAMS,
              target);
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
    DEBUG_PRINTF(DEBUG_INPUT, "Prefix:  %s\nCommand: %s\nParams (%d):\n",
                 prefix ? prefix : "<none>", command, nparams);
    int i;
    for (i = 0; i < nparams; i++){
        DEBUG_PRINTF(DEBUG_INPUT, "   %s\n", params[i]);
    }
    DEBUG_PRINTF(DEBUG_INPUT, "\n");
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
                reply(server_info, cli, cli_node,
                      ":%s %d %s :You have not registered\r\n",
                      server_info->hostname,
                      ERR_NOTREGISTERED,
                      target);
            }
            else if (nparams < cmds[i].minparams)
            {
                // ERROR - the client didn't specify enough parameters for this command
                reply(server_info, cli, cli_node,
                      ":%s %d %s %s :Not enough parameters\r\n",
                      server_info->hostname,
                      ERR_NEEDMOREPARAMS,
                      target,
                      command);
            }
            else // Call cmd_foo handler.
            {
                (*cmds[i].handler)(server_info, cli, cli_node, params, nparams);
            }
            // Clean zombies
            ITER_LOOP(it, server_info->zombies)
            {
                client_t* zombie = iter_get_item(it);
                iter_drop_curr(it);
                free(zombie);
            }
            ITER_END(it);
            return;
        }
    }
    if (i == NELMS(cmds)){
        // ERROR - unknown command
        GET_SAFE_NAME(safe_command, command)
        reply(server_info, cli, cli_node,
              ":%s %d %s %s :Unknown command\r\n",
              server_info->hostname,
              ERR_UNKNOWNCOMMAND,
              target,
              safe_command);
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
    return FALSE;
}


/**
 * Check if a channel name is valid
 *
 * <channel> ::=
 * ('#' | '&') <chstring>
 */
int is_channel_valid(char* ch_name)
{
    if ( *ch_name != '#' && *ch_name != '&' )
    return FALSE;
    for (int i = 1; i < RFC_MAX_NICKNAME + 1; i++)
    {
        if (ch_name[i] == '\0') return TRUE;
        if (i == RFC_MAX_NICKNAME) return FALSE;
        if (!IS_CH_CHAR(ch_name[i])) return FALSE;
    }
    return FALSE; // Never reached
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
void motd(server_info_t* server_info, client_t* cli, Node* cli_node, char* hostname)
{
    reply(server_info, cli, cli_node,
          ":%s %d %s :- %s Message of the day - \r\n",
          hostname,
          RPL_MOTDSTART,
          cli->nick,
          hostname);
    reply(server_info, cli, cli_node,
          ":%s %d %s :- %s\r\n",
          hostname,
          RPL_MOTD,
          cli->nick,
          MOTD_STR);
    reply(server_info, cli, cli_node,
          ":%s %d %s :End of /MOTD command\r\n",
          hostname,
          RPL_ENDOFMOTD,
          cli->nick);
}


/**
 * Remove a channel if it is empty.
 */
void remove_channel_if_empty(server_info_t* server_info, channel_t* ch)
{
    if (ch->members->size == 0)
    {
        // FIXME: pass in channel node and use drop_node()
        find_and_drop_item(server_info->channels, ch);
        free(ch->members);
        free(ch);
    }
}


/**
 * Remove a client from a channel.
 */
void remove_client_from_channel(server_info_t* server_info,
                                client_t* cli, Node* cli_node)
{
    if (cli->channel)
    {
        // Remove client from the channel member list
        drop_node(cli->channel->members, cli_node);
        // Remove channel if it becomes empty
        remove_channel_if_empty(server_info, cli->channel);
    }
}


/**
 * Find a channel by name.
 */
channel_t* find_channel_by_name(server_info_t* server_info, char* target_name)
{
    channel_t* ch_found = NULL;
    
    ITER_LOOP(it, server_info->channels)
    {
        channel_t* ch = (channel_t *) iter_get_item(it);
        if ( !strncmp(target_name, ch->name, RFC_MAX_NICKNAME+1) )
        {
            ch_found = ch;
            break;
        }
    } /* Iterator loop */
    ITER_END(it);
    
    return ch_found;
}


/**
 * Echo a message to all members in the client's channel (if any),
 * including the client him/herself if |echo_to_themselves| is set to TRUE.
 */
void echo_message(server_info_t* server_info,
                  client_t*      cli,
                  int            echo_to_themselves,
                  const char* restrict format, ...)
{
    // Retrieve args after |format|
    va_list args, args_copy;
    va_start(args, format);
    
    if (cli->channel)
    {
        // Loop through the channel members
        ITER_LOOP(it, cli->channel->members)
        {
            client_t* other = (client_t *) iter_get_item(it);
            Node* other_node = iter_get_node(it);
            // Echo message
            if (other != cli || echo_to_themselves)
            {
                va_copy(args_copy, args);
                vreply(server_info, other, other_node, format, args);
                va_copy(args, args_copy);
            }
        } /* Iterator loop */
        ITER_END(it);
    }
    // Else, the client isn't in any channel => Do nothing
    
    // Clean va_list
    va_end(args);
    va_end(args_copy);
}


/* Command handlers */

/**
 * Command NICK
 */
void cmdNick(CMD_ARGS)
{
    // ERROR - No nickname given
    if (!nparams)
    {
        reply(server_info, cli, cli_node,
              ":%s %d %s :No nickname given\r\n",
              server_info->hostname,
              ERR_NONICKNAMEGIVEN,
              *cli->nick? cli->nick: "*");
    }
    // ERROR - Invalid nickname
    else if (!is_nickname_valid(params[0]))
    {
        GET_SAFE_NAME(nick_safe, params[0]); // CHOICE: truncate if requested name too long
        reply(server_info, cli, cli_node,
              ":%s %d %s %s :Erroneus nickname\r\n",
              server_info->hostname,
              ERR_ERRONEOUSNICKNAME,
              *cli->nick? cli->nick: "*",
              nick_safe);
    }
    else /* nick valid */
    {
        // Check for nickname collision
        char* nick = params[0];
        ITER_LOOP(it, server_info->clients)
        {
            client_t* other = (client_t *) iter_get_item(it);
            if (other != cli &&
                *other->nick &&
                // CHOICE: we do not check |registered| here,
                // because two unregistered clients may still have colliding nicknames
                check_collision(nick, other->nick))
            {
                // ERROR - Nickname collision
                reply(server_info, cli, cli_node,
                      ":%s %d %s %s :Nickname is already in use\r\n",
                      server_info->hostname,
                      ERR_NICKNAMEINUSE,
                      *cli->nick? cli->nick: "*",
                      nick);
                ITER_END(it);
                return;
            }
        }
        ITER_END(it);
        
        /* No collision */
        
        // Make a copy of old nickname, if any
        char old_nick[RFC_MAX_NICKNAME+1];
        old_nick[RFC_MAX_NICKNAME] = '\0';
        if (*cli->nick)
        strcpy(old_nick, cli->nick);
        
        // Set client's nickname
        strcpy(cli->nick, nick); // CHOICE: new nick same as old nick => No effect
        
        // If user already is in a channel,
        // ECHO - NICK to everyone else in the same channel
        if (cli->channel)
        {
            echo_message(server_info, cli, FALSE,
                         ":%s!%s@%s NICK %s\r\n",
                         old_nick,
                         cli->user,
                         cli->hostname,
                         cli->nick);
        }
        // Otherwise, the client is not any channel
        // => Register the client if possible
        else if (!cli->registered && *cli->user)
        {
            cli->registered = 1;
            motd(server_info, cli, cli_node, server_info->hostname);
        }
    } /* nick valid */
}


/**
 * Command USER
 */
void cmdUser(CMD_ARGS){
    // ERROR - already registered
    if (cli->registered)
    {
        reply(server_info, cli, cli_node,
              ":%s %d %s :You may not reregister\r\n",
              server_info->hostname,
              ERR_ALREADYREGISTRED,
              cli->nick);
    }
    // Update user information
    strncpy(cli->user, params[0], MAX_USERNAME-1);
    strncpy(cli->realname, params[3], MAX_REALNAME-1);
    
    // CHOICE:
    // If the client is not registered but already has already issued USER, i.e.,
    // she hasn't run NICK but has run USER, then existing user infomation is
    // silently overwritten
    
    // Register the client if possible
    if (!cli->registered && *cli->nick)
    {
        cli->registered = 1;
        motd(server_info, cli, cli_node, server_info->hostname);
    }
}


/**
 * Command QUIT
 *
 * In this function, we
 *   1. Mark the client as zombie
 *   2. Remove the client from its channel (if any), and remove the channel if it becomes empty
 *   3. Echo QUIT message to everyone else in the same channel (if any)
 *   4. Set client's channel to NULL.
 *   5. Close the socket.
 */
void cmdQuit(CMD_ARGS)
{
    // Check if client is already a zombie
    // If this command was issued by the client, then the client is marked as zombie.
    if (!cli->zombie)
    {
        cli->zombie = TRUE;
        add_item(server_info->zombies, cli);
    }
    // Else, the command was faked by the server,
    // in which case the client has already been duly marked as a zombie.
    
    remove_client_from_channel(server_info, cli, cli_node);
    // ECHO - QUIT to channel members
    echo_message(server_info, cli, FALSE,
                 ":%s!%s@%s QUIT :Connection closed\r\n",
                 cli->nick,
                 cli->user,
                 cli->hostname);
    cli->channel = NULL;
    // Remove client from the server's client list
    drop_node(server_info->clients, cli_node);
    // Close the connection
    close(cli->sock);
    
    // free(cli) is done after a handler returns to handle_line,
    // during the zombie-cleaning stage
}


/**
 * Command JOIN
 */
void cmdJoin(CMD_ARGS)
{
    // CHOICE: If there is a list of targets, pick the first one and ignore the rest
    char* channel_to_join = params[0];
    char* comma = strchr(channel_to_join, ',');
    if (comma)
        *comma = '\0'; // Replace ',' with '\0' to take only the first channel name
    
    if ( !is_channel_valid(channel_to_join) )
    {
        GET_SAFE_NAME(chname_safe, channel_to_join);
        reply(server_info, cli, cli_node,
              ":%s %d %s %s :No such channel\r\n",
              server_info->hostname,
              ERR_NOSUCHCHANNEL,
              cli->nick,
              chname_safe);
    }
    else /* Channel name valid */
    {
        channel_t* ch_found = find_channel_by_name(server_info, channel_to_join);
        if (cli->channel) // Client was previously in a channel
        {
            // Join a channel of which the client is already a member => Do nothing
            if ( ch_found && !strcmp(cli->channel->name, ch_found->name) ) return;
            // ECHO - QUIT to members of the previous channel
            // (but client still connected, so cannot reuse cmdQuit)
            echo_message(server_info, cli, TRUE,
                         ":%s!%s@%s QUIT :Client left channel\r\n", // CHOICE: The joiner also gets back QUIT
                         cli->nick,
                         cli->user,
                         cli->hostname);
            remove_client_from_channel(server_info, cli, cli_node);
            cli->channel = NULL;
        }
        // Client is no longer in any channel at this point
        if (!ch_found) // Create the channel if it doesn't exist yet
        {
            channel_t* new_ch = malloc(sizeof(channel_t));
            LinkedList* members = malloc(sizeof(LinkedList));
            init_list(members);
            new_ch->members = members;
            strcpy(new_ch->name, channel_to_join);
            add_item(server_info->channels, new_ch);
            ch_found = new_ch;
        }
        // Channel to join (ch_found) exists at this point
        add_item(ch_found->members, cli); // Add client to the member list
        cli->channel = ch_found;
        
        // ECHO - JOIN to all members, including the newly joined client
        echo_message(server_info, cli, TRUE,
                     ":%s!%s@%s JOIN %s\r\n",
                     cli->nick,
                     cli->user,
                     cli->hostname,
                     ch_found->name);
        
        // REPLY - Send the list of channel members
        ITER_LOOP(jt, ch_found->members)
        {
            client_t* other = (client_t *) iter_get_item(jt);
            reply(server_info, cli, cli_node,
                  ":%s %d %s = %s :%s\r\n",
                  server_info->hostname,
                  RPL_NAMREPLY,
                  cli->nick,
                  ch_found->name,
                  other->nick);
        } /* Iterator loop */
        ITER_END(jt);
        
        // REPLY - End
        reply(server_info, cli, cli_node,
              ":%s %d %s %s :End of /NAMES list\r\n",
              server_info->hostname,
              RPL_ENDOFNAMES,
              cli->nick,
              ch_found->name);
    } /* Channel name valid */
}


/**
 * Command PART
 */
void cmdPart(CMD_ARGS)
{
    char* channel_to_part = params[0];
    char* comma = strchr(channel_to_part, ',');
    if (comma)
        *comma = '\0'; // Replace ',' with '\0' to take only the first target
    
    // Find the channel the client wishes to part
    channel_t* ch_found = find_channel_by_name(server_info, channel_to_part);
    if (!ch_found) // ERROR - No such channel
    {
        GET_SAFE_NAME(safe_chname, channel_to_part);
        reply(server_info, cli, cli_node,
              ":%s %d %s %s :No such channel\r\n",
              server_info->hostname,
              ERR_NOSUCHCHANNEL,
              cli->nick,
              safe_chname);
    }
    else if ( !find_item(ch_found->members, cli) ) // ERROR - Not on channel
    {
        reply(server_info, cli, cli_node,
              ":%s %d %s %s :You're not on that channel\r\n",
              server_info->hostname,
              ERR_NOTONCHANNEL,
              cli->nick,
              ch_found->name);
    }
    else // Client is indeed in the channel to part
    {
        echo_message(server_info, cli, TRUE,
                     ":%s!%s@%s QUIT :\r\n", // CHOICE: The joiner also gets back QUIT
                     cli->nick,
                     cli->user,
                     cli->hostname);
        
        remove_client_from_channel(server_info, cli, cli_node);
        cli->channel = NULL;
    }
}


/**
 * Command LIST
 */
void cmdList(CMD_ARGS)
{
    reply(server_info, cli, cli_node,
          ":%s %d %s Channel :Users Name\r\n",
          server_info->hostname,
          RPL_LISTSTART,
          cli->nick);
    
    ITER_LOOP(it, server_info->channels)
    {
        channel_t* ch = (channel_t *) iter_get_item(it);
        reply(server_info, cli, cli_node,
              ":%s %d %s %s %d :\r\n",
              server_info->hostname,
              RPL_LIST,
              cli->nick,
              ch->name,
              ch->members->size);
    } /* Iterator loop */
    ITER_END(it);
    
    reply(server_info, cli, cli_node,
          ":%s %d %s :End of /LIST\r\n",
          server_info->hostname,
          RPL_LISTEND,
          cli->nick);
}


/**
 * Command PRIVMSG
 */
void cmdPmsg(CMD_ARGS)
{
    /* CHOICE:
     * When there aren't enough params, there is no way to tell between
     * a target name and text_to_send.
     * Thus, we assume that the first param must be the target name.
     *   If nparams == 0, then reply ERR_NORECIPIENT.
     *   If nparams == 1, then reply ERR_NOTEXTTOSEND.
     */
    if (nparams == 0)
    {
        reply(server_info, cli, cli_node,
              ":%s %d %s :No recipient given (PRIVMSG)\r\n",
              server_info->hostname,
              ERR_NORECIPIENT,
              cli->nick);
        return;
    }
    else if (nparams == 1)
    {
        reply(server_info, cli, cli_node,
              ":%s %d %s :No text to send\r\n",
              server_info->hostname,
              ERR_NOTEXTTOSEND,
              cli->nick);
        return;
    }
    // Parse target list, delimited by ","
    char *target_list, *to_free;
    to_free = target_list = strdup(params[0]);
    char *target = strtok(target_list, ",");
    while (target)
    {
        if (!strcmp(target, cli->nick))
        {   // Do nothing if the target is the sending client
            target = strtok(NULL, ",");
            continue;
        }
        
        int target_found = FALSE;
        
        // Is the target a client?
        ITER_LOOP(it, server_info->clients)
        {
            client_t* other = (client_t *) iter_get_item(it);
            Node* other_node = iter_get_node(it);
            if (!strcmp(target, other->nick)) // Target found
            {
                target_found = TRUE;
                reply(server_info, other, other_node,
                      ":%s PRIVMSG %s :%s\r\n",
                      cli->nick,
                      target,
                      params[1]);
                break;
            }
        }
        ITER_END(it);
        
        // Is the target is a channel?
        channel_t* ch_found = find_channel_by_name(server_info, target);
        if (ch_found)
        {
            target_found = TRUE;
            ITER_LOOP(it, ch_found->members)
            {
                client_t* other = (client_t *) iter_get_item(it);
                Node* other_node = iter_get_node(it);
                if (other != cli)
                {
                    reply(server_info, other, other_node,
                          ":%s PRIVMSG %s :%s\r\n",
                          cli->nick,
                          target,
                          params[1]);
                }
            }
            ITER_END(it);
        }
        
        // Target name matches neither client nor a channel
        // ERROR - No such nick
        if (!target_found)
        {
            reply(server_info, cli, cli_node,
                  ":%s %d %s %s :No such nick/channel\r\n",
                  server_info->hostname,
                  ERR_NOSUCHNICK,
                  cli->nick,
                  target);
        }
        // Go to next target name
        target = strtok(NULL, ",");
        
    } /* while(target) */
    free(to_free);
}


/**
 * Command WHO
 */
void cmdWho(CMD_ARGS)
{
    // No <name> is given=> Return all visible users
    // As per RFC:
    //   In the absence of the <name> parameter, all visible (users who aren't invisible (user mode +i)
    //   and who don't have a common channel with the requesting client) are listed
    if (!nparams)
    {
        ITER_LOOP(it, server_info->clients)
        {
            client_t* other = iter_get_item(it);
            if (!cli->channel   || // No common channel if one of them doesn't have a channel
                !other->channel ||
                (other->channel != cli->channel))
            {
                // RFC: <channel> <user> <host> <server> <nick> <H|G>[*][@|+] :<hopcount> <real name>
                reply(server_info, cli, cli_node,
                      ":%s %d %s %s %s %s %s %s H :0 %s\r\n",
                      server_info->hostname,
                      RPL_WHOREPLY,
                      cli->nick,
                      other->channel->name,
                      other->user,
                      other->hostname,
                      server_info->hostname,
                      other->nick,
                      other->realname
                      );
            }
            reply(server_info, cli, cli_node,
                  "%s %d %s * :End of /WHO list\r\n",
                  server_info->hostname,
                  RPL_ENDOFWHO,
                  cli->nick);
        }
        ITER_END(it);
    }
    else
    {
        char *to_free, *target_list;
        to_free = target_list = strdup(params[0]);
        char* target = strtok(target_list, ",");
        while (target)
        {
            // As per CMPU-375 RFC Note:
            // Your server should match <name> against channel name only
            GET_SAFE_NAME(safe_query, target);
            channel_t* ch_match = find_channel_by_name(server_info, safe_query);
            if (ch_match)
            {
                // Loop through all members of that channel
                ITER_LOOP(it_cli, ch_match->members)
                {
                    client_t* other = (client_t *) iter_get_item(it_cli);
                    // RFC: <channel> <user> <host> <server> <nick> <H|G>[*][@|+] :<hopcount> <real name>
                    reply(server_info, cli, cli_node,
                          ":%s %d %s %s %s %s %s %s H :0 %s\r\n",
                          server_info->hostname, RPL_WHOREPLY, cli->nick,
                          other->channel->name,
                          other->user,
                          other->hostname,
                          server_info->hostname,
                          other->nick,
                          other->realname);
                } /* Iterator loop */
                ITER_END(it_cli);
            }
            // CHOICE: if |safe_query| doesn't match any channel, fall through
            reply(server_info, cli, cli_node,
                  ":%s %d %s %s :End of /WHO list\r\n",
                  server_info->hostname,
                  RPL_ENDOFWHO,
                  cli->nick,
                  safe_query);
            
            target = strtok(NULL, ",");
        }
        free(to_free);
    }
}
