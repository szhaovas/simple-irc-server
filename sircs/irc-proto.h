#ifndef _IRC_PROTO_H_
#define _IRC_PROTO_H_

#include "sircs.h"

typedef enum {
    ERR_INVALID = 1, // ?
    ERR_NOSUCHNICK = 401,
    ERR_NOSUCHCHANNEL = 403,
    ERR_NORECIPIENT = 411, // ?
    ERR_NOTEXTTOSEND = 412, // ?
    ERR_UNKNOWNCOMMAND = 421,
    ERR_ERRONEOUSNICKNAME = 432,
    ERR_NICKNAMEINUSE = 433,
    ERR_NONICKNAMEGIVEN = 431, // ?
    ERR_NOTONCHANNEL = 442,
    ERR_NOLOGIN = 444, // Not used
    ERR_NOTREGISTERED = 451,
    ERR_NEEDMOREPARAMS = 461,
    ERR_ALREADYREGISTRED = 462
} err_t;

typedef enum {
    RPL_NONE = 300, // Not used
    RPL_USERHOST = 302, // Not used
    RPL_LISTSTART = 321,
    RPL_LIST = 322,
    RPL_LISTEND = 323,
    RPL_WHOREPLY = 352,
    RPL_ENDOFWHO = 315,
    RPL_NAMREPLY = 353,
    RPL_ENDOFNAMES = 366,
    RPL_MOTDSTART = 375,
    RPL_MOTD = 372,
    RPL_ENDOFMOTD = 376
} rpl_t;


void handleLine(char* line, server_info_t* server_info, client_t* client);

#endif /* _IRC_PROTO_H_ */
