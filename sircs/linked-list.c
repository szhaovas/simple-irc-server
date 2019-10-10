#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "linked-list.h"


/* Constants */
#define TRUE 1
#define FALSE 0



/* LinkedList */

/**
 * Initialze a linked list.
 * This function be called once and only once before a linked list
 * may be used.
 */
void init_list(LinkedList* list)
{
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    list->__count = 0;
}



/**
 * Add an item, pointed to by |data|, to a linked list.
 */
Node* add_item(LinkedList* list, void* data)
{
    // Allocate a new node
    Node* node = malloc(sizeof(Node));
    node->data = data;
    node->prev = NULL;
    node->next = NULL;
    node->__id = list->__count;
    
    // Adding to an empty list
    if (list->head == NULL)
    {
        list->head = node;
        list->tail = node;
    }
    // At least one elements => Insert at head
    else
    {
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    list->size += 1;
    list->__count += 1;
    return node;
}


/**
 * Find an item in a linked list.
 */
int find_item(LinkedList* list, void* data)
{
    Iterator_LinkedList* it;
    for (it = iter(list); !iter_empty(it); it = iter_next(it))
    {
        if (iter_get(it) == data)
        {
            iter_clean(it);
            return TRUE;
        }
    }
    iter_clean(it);
    return FALSE;
}



/**
 * Drop a node from a linked list.
 */
void* drop_node(LinkedList* list, Node* node)
{
    void* data = node->data;
    // Fix link
    // |node| wasn't head
    if (node->prev != NULL)
        node->prev->next = node->next;
    // |node| was head => head now points to the next node
    else
        list->head = node->next;
    // |node| wasn't tail
    if (node->next != NULL)
        node->next->prev = node->prev;
    // |node| was tail => tail now points to the previous node
    else
        list->tail = node->prev;
    // Free memory
    free(node);
    list->size -= 1;
    return data;
}



/**
 * Convert a list to string and store it in the input buffer.
 */
char* list_to_str(LinkedList* list, char* buf)
{
    *buf++ = '[';
    for (Node* node = list->head; node != NULL; node = node->next)
    {
        if (node != list->head)
            buf += sprintf(buf, ", ");
        
        buf += sprintf(buf, "%i", node->__id);
    }
    *buf++ = ']';
    *buf++ = '\0';
    return buf;
}



/* Iterator_LinkedList */

/* Private functions */

/**
 * Yield the current item from the iterator.
 * Note: this function will increment the pointer, so the same node
 * will never be yielded twice.
 */
Node* get_and_incr(Iterator_LinkedList* it)
{
    assert(!it->incremented);
    it->incremented = TRUE;
    Node* curr = it->curr;
    it->curr = it->curr->next; // Advance by one node
    return curr;
}



/* Public functions */

/**
 * Obtain an iterator for the linked list |list|.
 */
Iterator_LinkedList* iter(LinkedList* list)
{
    Iterator_LinkedList* it = malloc(sizeof(Iterator_LinkedList));
    it->list = list;
    it->curr = list->head;
    it->incremented = FALSE;
    return it;
}



/**
 * Check if an iterator contains any more node.
 */
int iter_empty(Iterator_LinkedList* it)
{
    return !it->curr;
}


/**
 * Clean iterator. Must be called once an iterator is no longer in use.
 */
void iter_clean(Iterator_LinkedList* it)
{
    free(it);
}


/**
 * Get the next iterator.
 */
Iterator_LinkedList* iter_next(Iterator_LinkedList* it)
{
    if (!it->incremented)
        get_and_incr(it);
    it->incremented = FALSE;
    return it;
}


/**
 * Return the current item from the iterator.
 */
void* iter_get(Iterator_LinkedList* it)
{
    return it->curr->data;
}


/**
 * Add an item, pointed to by |data|, to the list referred to by the iterator.
 */
Node* iter_add(Iterator_LinkedList* it, void* data)
{
    return add_item(it->list, data);
}



/**
 * Drop the current item from an iterator.
 */
void* iter_drop(Iterator_LinkedList* it)
{
    Node* to_be_dropped = get_and_incr(it);
    return drop_node(it->list, to_be_dropped);
}


/**
 * Drop an item from a linked list by iterating through it
 */
void drop_item(LinkedList* list, void* data)
{
    Iterator_LinkedList* it;
    for (it = iter(list);
         !iter_empty(it);
         it = iter_next(it))
    {
        if (iter_get(it) == data)
        {
            iter_drop(it);
            break;
        }
    }
    iter_clean(it);
}
