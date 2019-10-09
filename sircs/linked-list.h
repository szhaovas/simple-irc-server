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


/* Linked List Iterator */
typedef struct {
    LinkedList* list;
    Node* curr;
    int incremented;
} Iterator_LinkedList;


/* Function declarations */

void init_list(LinkedList* list);

Node* add_item(LinkedList* list, void* data);

void* drop_node(LinkedList* list, Node* node);

char* list_to_str(LinkedList* list, char* buf);

Iterator_LinkedList* iter(LinkedList* list);

int iter_empty(Iterator_LinkedList* it);

Iterator_LinkedList* iter_next(Iterator_LinkedList* it);

void* iter_get(Iterator_LinkedList* it);

Node* iter_add(Iterator_LinkedList* it, void* data);

void* iter_drop(Iterator_LinkedList* it);


#endif /* _LINKED_LIST_H_ */

