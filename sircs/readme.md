## Simple IRC Server
CMPU-375 Computer Networks (2019 Fall)

Professor Meireles

Team 1: Junrui Liu, Tahsin Oshin, Shihan Zhao

## Design

The server maintains a list of clients and channels. It handles activities and data from the clients via I/O multiplexing (specifically, the `select()` syscall). The concurrency part mostly borrows the framework introduced in the lectures.

If possible, data from the client are parsed by `handle_data()` into well-formed messages, which are further dispatched to the appropriate command handlers.

The handlers serve the user commands according to the specification of RFC 1459. They send replies or echo messages via `reply()` and `vreply()`, which additionally check any errors when writing to the recipient's socket, and remove disconnected clients and echo QUIT messages on their behalves.

## Implementation Details

### Data Structures
We use doubly linked lists (`Linked_List`) as our core data structure to support efficient node traversal, addition and removal. Since operations to remove nodes *while* traversing a list appear quite often, we also implemented `Iterator_LinkedList` to reduce code repetition while avoiding common pitfalls, especially in node deletion.

One of the foremost reason we implemented an iterator interface for the linked list data structure is that the same node may be referenced simultaneously by multiple loop pointers, so removing nodes and fixing links eagerly at one place may adversely affect the node pointers at other places. We thus took a lazy approach when implementing the linked list and the iterators: each node has a `valid` flag, and each linked list keeps track of the number of active iterators that refer to it. Deleting a node will only mark a node as invalid. When an iterator completes its job, we then check if it is the last iterator referring to the list. If so, invalid nodes can be safely deleted.

### The Server
The server keeps the following lists:
1. `clients`: the list of connected clients,
2. `channels`: the list of channels, and
3. `zombies`: the list of clients that has disconnected but state information not yet removed. When we detect a disconnection, the client is marked as a `zombie`, and moved from the `clients` list to the `zombies` list.
   
The reason we do not remove a client's states immediately upon detecting a disconnection is that subsequent code may attempt to access the data associated with the now deleted client. To prevent memory faults, the function issuing the write often has to return immediately to avoid further (illegal) references to the non-existent client, while taking care to clean up allocated resources. This makes the control flow less obvious and debugging more difficult. 

Thus, we choose to postpone removing a client's state to permit the flow through the normal code path, with the caveat that `write()` addressed to a zombie client will not be actuated. (The otherwise gruesome zombie analogy is, in fact, befitting: zombies can be observed, but they make very poor conversation partners.) Only after a command handler returns do we remove the zombie clients' states.

Together with the server's hostname, the above information is stored in a `server_info_t` structure, shared by almost all non-trivial functions we have defined.

### Clients
The client structure has type `client_t`, and is largely the same as in the starter code, with the channel name being replaced by a pointer to the actual channel structure. Since a client is referenced by server's client list as well as a channel's member list, we also store backward pointers to nodes the respective lists to enable fast node removal. Each client also has two flags:
- the  `zombie` flag that indicates the connection has closed but state info not yet removed
- the `keep_throwing` flag indicates that the server has received from the client a segment (not terminated by `\r` or `\n`) of a message already exceeding the message size limit, as defined in the constant `RFC_MAX_MSG_LEN` to be 512 bytes. The remaining portion of the same message, once delivered, must also be thrown away to prevent buffer overflow. The rationale is discussed in the next section.

### Channels

The channel structure has type `channel_t`, which includes the name of the channel, the list of members, and a backward pointer to the node in server's list of channels.


## Implementation choices concerning the RFC

We have placed the word `CHOICE` next to the code for which the RFC does not specify the standard behavior. The following list summarizes the implementation choices we have made:

1. Messages strictly longer than 512 bytes will be discarded in their entireties. Even if a large message is received via multiple reads, we treat all segments as a single message and discard until a message delimiter is reached.

    Initially, we only threw away the first 512 bytes, but later realized that the remaining portion might itself constitute a "valid" command, which we would not want to process. Also, upon receiving a PRIVMSG message, our server does nothing more than forwarding it to the recipient(s). Thus, restricting our attention to size-conforming client messages ensures that the server does not violate the protocol.

2. If a server reply must quote a string sent by the client, e.g., an invalid command or nickname, then we truncate a long string to keep server message from exceeding the message size limit. If the string is empty, we replace it with the character `*`.

3. If the user specifies too many parameters for a command, we only consume the necessary parameters, and ignore the redundant ones.

4. Command NICK: If a client attempts to request the same nickname again, no reply is generated.

5. Command NICK: If a client `A` attempts to request a nickname belonging to another _unregistered_ client `B` (who has already got a valid nickname but has not issued a valid USER), then `A` will still get a nickname-already-in-use error.

6. Command USER: If the client issues multiple USER commands before being registered (i.e. before issuing a valid NICK), then the existing client information is silently overwritten by each USER command.

7. Command JOIN: If the parameter is a list of channels, we only help the client join the first channel, and ignore the rest.

8.  Command PART: If the parameter is a list of channels, we attempt to remove the client from each channel (even though the client can be a member of at most one channel from the list). A not-on-channel error is generated for each channel the client isn't on.

9.  Command JOIN & PART: If a client parts a channel (either explicitly via PART, or implicitly if the client switches to another channel), we always echo QUIT to the channel members, including the client him/herself, even though PART would be a more logical choice here.

10. Command PRIVMSG: When there aren't enough params, there is no way to tell between a target name and the text to send. Thus, we assume that the first param is the target name, i.e.,
    - If no parameter is specified, then we reply ERR_NORECIPIENT.
    - If only one parameter is specified, then we reply ERR_NOTEXTTOSEND.

## Known Issues
1. Depending on the (rare) timing of disconnection, the program may segfault in function `vreply()`.
2. The timeout structure for `select()` did not work as well on Vassar's Linux machines as on macOS (returning much sooner than expected). Thus, we pass a `NULL` timeval pointer to `select()`.