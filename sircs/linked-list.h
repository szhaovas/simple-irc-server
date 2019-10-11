#ifndef _LINKED_LIST_H_
#define _LINKED_LIST_H_

/* Node */
struct _node_struct {
    struct _node_struct* prev;
    struct _node_struct* next;
    void* data;
    int __id;
};

typedef struct _node_struct Node;


/* Linked List */
typedef struct {
    Node* head;
    Node* tail;
    int size;
    int __count;
} LinkedList;


void init_list(LinkedList* list);

Node* add_item(LinkedList* list, void* data);

int find_item(LinkedList* list, void* data);

void drop_item(LinkedList* list, void* data);

void* drop_node(LinkedList* list, Node* node);

char* list_to_str(LinkedList* list, char* buf);


/**
 * Linked List Iterator.
 *
 * Example Usage
 
     LinkedList* list;
     // Adding items to |list| ...
     Iterator_LinkedList* it;
     for (it = iter(list); !iter_empty(it); it = iter_next(it))
     {
        // iter_get, iter_add, iter_drop, etc.
     }
     iter_clean(it);
 
*/

typedef struct {
    LinkedList* list;
    Node* curr;
    int incremented;
} Iterator_LinkedList;

Iterator_LinkedList* iter(LinkedList* list);

void iter_clean(Iterator_LinkedList* it);

int iter_empty(Iterator_LinkedList* it);

void iter_next(Iterator_LinkedList* it);

void* iter_get(Iterator_LinkedList* it);

Node* iter_add(Iterator_LinkedList* it, void* data);

void* iter_drop(Iterator_LinkedList* it);


#endif /* _LINKED_LIST_H_ */

