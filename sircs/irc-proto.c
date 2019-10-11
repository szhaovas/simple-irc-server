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
#define CMD_ARGS server_info_t* server_info, client_t* cli, char **params, int nparams
typedef void (*cmd_handler_t)(CMD_ARGS);
#define COMMAND(cmd_name) void cmd_name(CMD_ARGS)

// Server reply macro
#define WRITE(sock, fmt, ...) do { dprintf(sock, fmt, ##__VA_ARGS__); } while (0)
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
        GET_SAFE_NAME(nick_safe, params[0]);
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
        for (it = iter(server_info->clients); !iter_empty(it); iter_next(it))
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
            for (it = iter(cli->channel->members); !iter_empty(it); iter_next(it))
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

    // CHOICE:
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
        channel_t* prev_ch = cli->channel;
        // Loop through members from the client's channel
        Iterator_LinkedList* it;
        for (it = iter(cli->channel->members);
             !iter_empty(it);
             iter_next(it))
        {
            client_t* other = (client_t *) iter_get(it);

            // Client herself => Remove client from the channel member list
            if (cli == other)
            {
                iter_drop(it);
                cli->channel = NULL;
            }

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

        if (prev_ch->members == 0)
            remove_empty_channel(server_info, prev_ch);
    } /* if (cli->channel) */
    free(cli);
}



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
        WRITE(cli->sock,
              ":%s %d %s %s :No such channel\r\n",
              server_info->hostname,
              ERR_NOSUCHCHANNEL,
              cli->nick,
              chname_safe);
    }
    else /* Channel name valid */
    {
        // Find the channel the client wishes to join
        channel_t* ch_found = NULL;
        Iterator_LinkedList* it;
        for (it = iter(server_info->channels);
             !iter_empty(it);
             iter_next(it))
        {
            channel_t* ch = (channel_t *) iter_get(it);
            if ( !strncmp(channel_to_join, ch->name, RFC_MAX_NICKNAME+1) )
            {
                ch_found = ch;
                break;
            }
        } /* Iterator loop */
        iter_clean(it);

        if (cli->channel) /* Client was previously in a channel */
        {
            // Join a channel of which the client is already a member => Do nothing
            if ( ch_found && !strcmp(cli->channel->name, ch_found->name) ) return;

            // Echo QUIT to members of the previous channel
            channel_t* prev_ch = cli->channel;
            Iterator_LinkedList* it;
            for (it = iter(prev_ch->members); !iter_empty(it); iter_next(it))
            {
                client_t* other = (client_t *) iter_get(it);
                if (other == cli)
                {
                    iter_drop(it);
                    cli->channel = NULL;
                }
                else
                {
                    WRITE(other->sock,
                          ":%s!%s@%s QUIT\r\n",
                          cli->nick,
                          cli->user,
                          cli->hostname);
                }
            } /* Iterator loop */
            iter_clean(it);

            // Remove the channel if it becomes empty
            if (prev_ch->members->size == 0)
                remove_empty_channel(server_info, prev_ch);
        } /* Client was previously in a channel */

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

        // Building RPL_NAMREPLY message(s)
        //        char common[RFC_MAX_MSG_LEN+1];
        //        sprintf(common,
        //                ":%s %d %s = %s :",
        //                server_info->hostname,
        //                RPL_NAMREPLY,
        //                cli->nick,
        //                ch_found->name);
        //        size_t common_len = strlen(common);
        //        char build_buf[RFC_MAX_MSG_LEN+1];

        for (it = iter(ch_found->members); !iter_empty(it); iter_next(it))
        {
            client_t* other = (client_t *) iter_get(it);

            // ECHO - JOIN to all members, including the newly joined client
            WRITE(other->sock,
                  ":%s!%s@%s JOIN %s\r\n",
                  cli->nick,
                  cli->user,
                  cli->hostname,
                  ch_found->name);

            // REPLY - Send channel members to the newly joined client
            // Since there may be too many nicknames in the channel to fit into a single message,
            // we build smaller messages, and send them out individually.
            // From RFC: <=|@> <channel> :[[@|+]<nick> [[@|+]<nick> [...]]]
            //
            //            build_and_send_message(cli->sock,
            //                                   build_buf,   RFC_MAX_MSG_LEN+1,
            //                                   common,      common_len,
            //                                   other->nick, strlen(other->nick),
            //                                   " ");

            WRITE(cli->sock,
                  ":%s %d %s = %s :%s\r\n",
                  server_info->hostname,
                  RPL_NAMREPLY,
                  cli->nick,
                  ch_found->name,
                  other->nick);
        } /* Iterator loop */
        iter_clean(it);
        //        build_and_send_message(cli->sock,
        //                              build_buf,   RFC_MAX_MSG_LEN+1,
        //                              common,      common_len,
        //                              "", 0,
        //                              " ");

        WRITE(cli->sock,
              ":%s %d %s %s :End of /NAMES list\r\n", // FIXME: explanation not helpful
              server_info->hostname,
              RPL_ENDOFNAMES,
              cli->nick,
              ch_found->name);
    } /* Channel name valid */
}



void cmdPart(CMD_ARGS)
{
    char* channel_to_part = params[0];

    // CHOICE: If there is a list of targets, pick the first one and ignore the rest
    char* comma = strchr(channel_to_part, ',');
    if (comma)
        *comma = '\0'; // Replace ',' with '\0' to take only the first target

    // Find the channel the client wishes to part
    channel_t* ch_found = NULL;
    Iterator_LinkedList* it;
    for (it = iter(server_info->channels); !iter_empty(it); iter_next(it))
    {
        channel_t* ch = (channel_t *) iter_get(it);
        if (!strncmp(ch->name, channel_to_part, RFC_MAX_NICKNAME+1))
        {
            ch_found = ch;
            break;
        }
    } /* Iterator loop */
    iter_clean(it);

    if (!ch_found) // ERROR - No such channel
    {
        GET_SAFE_NAME(safe_chname, channel_to_part);
        WRITE(cli->sock,
              ":%s %d %s %s :No such channel\r\n",
              server_info->hostname,
              ERR_NOSUCHCHANNEL,
              cli->nick,
              safe_chname);
    }
    else if ( !find_item(ch_found->members, cli) ) // ERROR - Not on channel
    {
        WRITE(cli->sock,
              ":%s %d %s %s :You're not on that channel\r\n",
              server_info->hostname,
              ERR_NOTONCHANNEL,
              cli->nick,
              ch_found->name);
    }
    else // Client is indeed in the channel to part
    {
        // Echo
        Iterator_LinkedList* it;
        for (it = iter(ch_found->members); !iter_empty(it); iter_next(it))
        {
            client_t* other = (client_t *) iter_get(it);

            // ECHO - PART to every one on the same channel (CHOICE: including the parting client)
            WRITE(other->sock,
                  ":%s!%s@%s PART %s\r\n",
                  cli->nick,
                  cli->user,
                  cli->hostname,
                  ch_found->name);

            // FIXME: RFC requires that echoing must be done before a client is removed (CHOICE: which we have not implemented here). Why?
            if (other == cli)
            {
                iter_drop(it);
                cli->channel = NULL;
            }
        } /* Iterator loop */
        iter_clean(it);

        // Remove the channel if it is empty
        if (ch_found->members->size == 0)
            remove_empty_channel(server_info, ch_found);
    }

}



void cmdList(CMD_ARGS)
{
    WRITE(cli->sock,
          "%s %d %s Channel :Users\r\n",
          server_info->hostname,
          RPL_LISTSTART,
          cli->nick);

    Iterator_LinkedList* it;
    for (it = iter(server_info->channels); !iter_empty(it); iter_next(it))
    {
        channel_t* ch = (channel_t *) iter_get(it);
        WRITE(cli->sock,
              "%s %d %s %s %d :\r\n",
              server_info->hostname,
              RPL_LIST,
              cli->nick,
              ch->name,
              ch->members->size);
    } /* Iterator loop */
    iter_clean(it);

    WRITE(cli->sock,
          "%s %d %s :End of /LIST\r\n",
          server_info->hostname,
          RPL_LISTEND,
          cli->nick);
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
    char* item_start = params[0]; // Start of possilby a list
    char* item_end;
    do
    {
        item_end = strchr(item_start, ',');
        if (item_end)
            *item_end = '\0';
        GET_SAFE_NAME(safe_query, item_start);

        // Loop through all channels
        Iterator_LinkedList* it_ch;
        for (it_ch = iter(server_info->channels); !iter_empty(it_ch); iter_next(it_ch))
        {
            channel_t* channel = (channel_t *) iter_get(it_ch);
            if (!strcmp(channel->name, safe_query)) // Channel name matches
            {
                // Loop through all members of that channel
                Iterator_LinkedList* it_cli;
                for (it_cli = iter(channel->members); !iter_empty(it_cli); iter_next(it_cli))
                {
                    client_t* other = (client_t *) iter_get(it_cli);
                    // RFC: <channel> <user> <host> <server> <nick> <H|G>[*][@|+] :<hopcount> <real name>
                    WRITE(cli->sock,
                          "%s %d %s %s %s %s %s %s H :0 %s :End of /WHO list\r\n",
                          server_info->hostname, RPL_WHOREPLY, cli->nick,
                          channel->name,
                          other->user,
                          other->hostname,
                          server_info->hostname,
                          other->nick,
                          other->realname
                          );
                } /* Iterator loop */
                iter_clean(it_cli);
            }
        } /* Iterator loop */
        iter_clean(it_ch);

        WRITE(cli->sock,
              "%s %d %s %s :End of /WHO list\r\n",
              server_info->hostname,
              RPL_ENDOFWHO,
              cli->nick,
              safe_query);

        if (item_end)
            item_start = item_end + 1;

    } while (item_end);
}
