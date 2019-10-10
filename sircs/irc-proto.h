#ifndef _IRC_PROTO_H_
#define _IRC_PROTO_H_

#include "sircs.h"

typedef enum {
    ERR_INVALID = 1,
    ERR_NOSUCHNICK = 401, //"<nickname> :No such nick/channel"
                          //Used to indicate the nickname parameter supplied to a command is currently unused.
    ERR_NOSUCHCHANNEL = 403, //"<channel name> :No such channel"
                             //Used to indicate the given channel name is invalid.
    ERR_NORECIPIENT = 411, //":No recipient given (<command>)"

    ERR_NOTEXTTOSEND = 412, //":No text to send"

    ERR_UNKNOWNCOMMAND = 421, //"<command> :Unknown command"
                              //Returned to a registered client to indicate that the command sent is unknown by the server.
    ERR_ERRONEOUSNICKNAME = 432, //"<nick> :Erroneus nickname"
                                 //Returned after receiving a NICK message which contains characters which do not fall in the deﬁned set. See Section 2.3.1 for details on valid nicknames.
    ERR_NICKNAMEINUSE = 433, //"<nick> :Nickname is already in use"
                             //Returned when a NICK message is processed that results in an attempt to change to a currently existing nickname.
    ERR_NONICKNAMEGIVEN = 431, //":No nickname given"
                               //Returned when a nickname parameter expected for a command and isn't found.
    ERR_NOTONCHANNEL = 442, //"<channel> :You're not on that channel"
                            //Returned by the server whenever a client tries to perform a channel effecting command for which the client isn't a member.
    ERR_NOLOGIN = 444, //"<user> :User not logged in"
                       //Returned by the summon after a SUMMON command for a user was unable to be performed since they were not logged in.
    ERR_NOTREGISTERED = 451, //":You have not registered"
                             //Returned by the server to indicate that the client must be registered before the server will allow it to be parsed in detail.
    ERR_NEEDMOREPARAMS = 461, //"<command> :Not enough parameters"
                              //Returned by the server by numerous commands to indicate to the client that it didn't supply enough parameters.
    ERR_ALREADYREGISTRED = 462 //":You may not reregister"
                               //Returned by the server to any link which tries to change part of the registered details (such as password or user details from second USER message).
} err_t;

typedef enum {
    RPL_NONE = 300,
    RPL_USERHOST = 302,
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
