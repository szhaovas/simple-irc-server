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

Thus, we choose to postpone removing a client's state to permit the flow through the normal code path, with the caveat that `write()` addressed to a zombie client will not be actuated. (The otherwise gruesome zombie analogy is, in fact, befitting: a zombie can be observed and feared, but it makes a very poor conversation partner.) Only after a command handler returns do we remove the zombie clients' states.

Together with the server's hostname, the above information is stored in a `server_info_t` structure, shared by almost all non-trivial functions we have defined.

### Clients
The client structure has type `client_t`, and is largely the same as in the starter code, with the channel name being replaced by a pointer to the actual channel structure.

### Channels

The channel structure has type `channel_t`, which includes the name of the channel as well as the non-empty list of members.


## Implementation choices concerning the RFC

We have placed the word `CHOICE` next to the code for which the RFC does not specify the standard behavior. The following lists summarize the implementation choices we have made:

1. If a server reply must quote a string sent by the client, e.g., an invalid command or nickname, then we truncate a long string to keep server message from exceeding the message size limit. If the string is empty, we replace it with the character `*`.

2. If the client issues multiple USER commands before being registered (i.e. issuing a valid NICK command), then existing client information is silently overwritten by each USER command.

3. For JOIN and PART, if the client has specified a target list, we take only the first item and ignore the rest.

<!-- 4. If a client parts a channel (both explicitly it was a PART message or implicitly if the client joins another channel), we echo PART instead of QUIT to channel -->

4. If a client attempts to set the same nickname, no reply is generated.


## Known Issues
1. Depending on the timing of disconnection, the function `vreply()` may segfault.