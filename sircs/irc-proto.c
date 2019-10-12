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
 * Send a reply.
 */
#define reply(sock, fmt, ...) do { dprintf(sock, fmt, ##__VA_ARGS__); } while (0)
#define vreply(sock, fmt, va) do { vdprintf(sock, fmt, va); } while (0)


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
        reply(cli->sock,
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
        reply(cli->sock,
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
                reply(cli->sock,
                      ":%s %d %s :You have not registered\r\n",
                      server_info->hostname,
                      ERR_NOTREGISTERED,
                      target);
            }
            else if (nparams < cmds[i].minparams)
            {
                // ERROR - the client didn't specify enough parameters for this command
                reply(cli->sock,
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
        reply(cli->sock,
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
    if ( *ch_name != '#' && *ch_name != '&' )
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
void motd(client_t* cli, char* hostname)
{
    reply(cli->sock,
          ":%s %d %s :- %s Message of the day - \r\n",
          hostname,
          RPL_MOTDSTART,
          cli->nick,
          hostname);
    
    reply(cli->sock,
          ":%s %d %s :- %s\r\n",
          hostname,
          RPL_MOTD,
          cli->nick,
          MOTD_STR);
    
    reply(cli->sock,
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
        drop_item(server_info->channels, ch);
        free(ch->members);
        free(ch);
    }
}


/**
 * Remove a client from a channel.
 */
void remove_client_from_channel(server_info_t* server_info, client_t* cli, channel_t* ch)
{
    if (cli->channel && cli->channel == ch)
    {
        // Remove client from the channel member list
        drop_item(cli->channel->members, cli);
        
        // Remove channel if it becomes empty
        remove_channel_if_empty(server_info, cli->channel);
    }
}


/**
 * Find a channel by name.
 */
channel_t* find_channel_by_name(server_info_t* server_info, char* target_name)
{
    ITER_LOOP(it, server_info->channels)
    {
        channel_t* ch = (channel_t *) iter_get(it);
        if ( !strncmp(target_name, ch->name, RFC_MAX_NICKNAME+1) )
        {
            return ch;
        }
    } /* Iterator loop */
    iter_clean(it);
    return NULL;
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
    if (cli->channel)
    {
        // Retrieve args after |format|
        va_list args, args_copy;
        va_start(args, format);
        
        // Loop through members from the client's channel
        ITER_LOOP(it, cli->channel->members)
        {
            client_t* other = (client_t *) iter_get(it);
            // Echo message
            if (other != cli || echo_to_themselves)
            {
                va_copy(args_copy, args);
                vreply(other->sock, format, args);
                va_copy(args, args_copy);
            }
            
        } /* Iterator loop */
        iter_clean(it);
        
        va_end(args); // Clean va_list
        va_end(args_copy);
    }
    // Else, the client isn't any channel.
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
        GET_SAFE_NAME(nick_safe, params[0]);
        reply(cli->sock,
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
        ITER_LOOP(it, server_info->clients)
        {
            client_t* other = (client_t *) iter_get(it);
            if (cli == other) continue;
            if (*other->nick && // Note: we should not check |registered| here,
                // because two unregistered clients may still have colliding nicknames
                check_collision(nick, other->nick))
            {
                reply(cli->sock,
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
            ITER_LOOP(it, cli->channel->members)
            {
                client_t* other = (client_t *) iter_get(it);
                if (cli == other) continue;
                reply(other->sock,
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
            motd(cli, server_info->hostname);
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
        reply(cli->sock,
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
        motd(cli, server_info->hostname);
    }
}



/**
 * Command QUIT
 */
void cmdQuit(CMD_ARGS)
{
    remove_client_from_channel(server_info, cli, cli->channel);
    
    echo_message(server_info, cli, FALSE,
                 ":%s!%s@%s QUIT :Connection closed\r\n",
                 cli->nick,
                 cli->user,
                 cli->hostname);
    
    cli->channel = NULL;
    
    // Remove client from the server's client list
    // (Junrui) FIXME: This iterates over the whole list and defeats the purpose?
    // Backward pointer?
    drop_item(server_info->clients, cli);
    
    // Close the connection
    close(cli->sock);
    
    free(cli);
}



/**
 * Command JOIN
 */
void cmdJoin(CMD_ARGS)
{
    char* channel_to_join = params[0];
    
    // CHOICE: If there is a list of targets, pick the first one and ignore the rest
    char* comma = strchr(channel_to_join, ',');
    if (comma)
        *comma = '\0'; // Replace ',' with '\0' to take only the first target
    
    if ( !is_channel_valid(channel_to_join) )
    {
        GET_SAFE_NAME(chname_safe, channel_to_join);
        reply(cli->sock,
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
            
            remove_client_from_channel(server_info, cli, cli->channel);
            
            // Echo QUIT to members of the previous channel
            echo_message(server_info, cli, FALSE,
                         ":%s!%s@%s QUIT :Client left channel\r\n",
                         cli->nick,
                         cli->user,
                         cli->hostname);
            
            cli->channel = NULL;
        }
        
        // Client is no longer in any channel by this point
        
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
            client_t* other = (client_t *) iter_get(jt);
            
            reply(cli->sock,
                  ":%s %d %s = %s :%s\r\n",
                  server_info->hostname,
                  RPL_NAMREPLY,
                  cli->nick,
                  ch_found->name,
                  other->nick);
        } /* Iterator loop */
        iter_clean(jt);
        
        // REPLY - End
        reply(cli->sock,
              ":%s %d %s %s :End of channel members\r\n",
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
    
    // CHOICE: If there is a list of targets, pick the first one and ignore the rest
    char* comma = strchr(channel_to_part, ',');
    if (comma)
        *comma = '\0'; // Replace ',' with '\0' to take only the first target
    
    // Find the channel the client wishes to part
    channel_t* ch_found = find_channel_by_name(server_info, channel_to_part);
    
    if (!ch_found) // ERROR - No such channel
    {
        GET_SAFE_NAME(safe_chname, channel_to_part);
        reply(cli->sock,
              ":%s %d %s %s :No such channel\r\n",
              server_info->hostname,
              ERR_NOSUCHCHANNEL,
              cli->nick,
              safe_chname);
    }
    else if ( !find_item(ch_found->members, cli) ) // ERROR - Not on channel
    {
        reply(cli->sock,
              ":%s %d %s %s :You're not on that channel\r\n",
              server_info->hostname,
              ERR_NOTONCHANNEL,
              cli->nick,
              ch_found->name);
    }
    else // Client is indeed in the channel to part
    {
        remove_client_from_channel(server_info, cli, cli->channel);
        
        echo_message(server_info, cli, TRUE,
                     ":%s!%s@%s PART %s\r\n",
                     cli->nick,
                     cli->user,
                     cli->hostname,
                     cli->channel->name);
        
        cli->channel = NULL;
    }
    
}



/**
 * Command LIST
 */
void cmdList(CMD_ARGS)
{
    reply(cli->sock,
          "%s %d %s Channel :Users\r\n",
          server_info->hostname,
          RPL_LISTSTART,
          cli->nick);
    
    ITER_LOOP(it, server_info->channels)
    {
        channel_t* ch = (channel_t *) iter_get(it);
        reply(cli->sock,
              "%s %d %s %s %d :\r\n",
              server_info->hostname,
              RPL_LIST,
              cli->nick,
              ch->name,
              ch->members->size);
    } /* Iterator loop */
    iter_clean(it);
    
    reply(cli->sock,
          "%s %d %s :End of /LIST\r\n",
          server_info->hostname,
          RPL_LISTEND,
          cli->nick);
}



/**
 * Command PRIVMSG
 */
void cmdPmsg(CMD_ARGS)
{
    /* do something */
    
}



/**
 * Command WHO
 */
void cmdWho(CMD_ARGS)
{
    // No <name> is given, so return all visible users
    // As per RFC:
    // In the absence of the <name> parameter, all visible (users who aren't invisible (user mode +i)
    // and who don't have a common channel with the requesting client) are listed
    if (!nparams)
    {
        ITER_LOOP(it, server_info->clients)
        {
            client_t* other = iter_get(it);
            if (cli->channel && other->channel && other->channel != cli->channel)
            {
                // RFC: <channel> <user> <host> <server> <nick> <H|G>[*][@|+] :<hopcount> <real name>
                reply(cli->sock,
                      "%s %d %s %s %s %s %s %s H :0 %s :End of /WHO list\r\n",
                      server_info->hostname, RPL_WHOREPLY, cli->nick,
                      other->channel->name,
                      other->user,
                      other->hostname,
                      server_info->hostname,
                      other->nick,
                      other->realname
                      );
            }
            reply(cli->sock,
                  "%s %d %s * :End of /WHO list\r\n",
                  server_info->hostname,
                  RPL_ENDOFWHO,
                  cli->nick);
        }
        iter_clean(it);
    }
    else
    {
        char* item_start = params[0]; // Start of possilby a list
        char* item_end;
        do
        {
            item_end = strchr(item_start, ',');
            if (item_end)
                *item_end = '\0';
            GET_SAFE_NAME(safe_query, item_start);
            
            channel_t* ch_match = find_channel_by_name(server_info, safe_query);
            // As per RFC annotation:
            // Your server should match <name> against channel name only
            if (ch_match)
            {
                // Loop through all members of that channel
                ITER_LOOP(it_cli, ch_match->members)
                {
                    client_t* other = (client_t *) iter_get(it_cli);
                    // RFC: <channel> <user> <host> <server> <nick> <H|G>[*][@|+] :<hopcount> <real name>
                    reply(cli->sock,
                          "%s %d %s %s %s %s %s %s H :0 %s :End of /WHO list\r\n",
                          server_info->hostname, RPL_WHOREPLY, cli->nick,
                          ch_match->name,
                          other->user,
                          other->hostname,
                          server_info->hostname,
                          other->nick,
                          other->realname
                          );
                } /* Iterator loop */
                iter_clean(it_cli);
            }
            
            // CHOICE: if |safe_query| doesn't match any channel, fall through
            
            reply(cli->sock,
                  "%s %d %s %s :End of /WHO list\r\n",
                  server_info->hostname,
                  RPL_ENDOFWHO,
                  cli->nick,
                  safe_query);
            
            if (item_end)
                item_start = item_end + 1;
            
        } while (item_end);
    }
}
