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
    list->size = 0;
    list->__id_gen = 0;
    list->__references = 0;
    list->__has_invalid = FALSE;
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
    node->__id = list->__id_gen;
    node->__valid = 1;
    
    // Adding to an empty list
    if (list->head == NULL)
    {
        list->head = node;
    }
    // At least one elements => Insert at head
    else
    {
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    list->size += 1;
    list->__id_gen += 1;
    return node;
}


/**
 * Find an item in a linked list.
 */
int find_item(LinkedList* list, void* data)
{
    Iterator_LinkedList* it;
    for (it = iter(list); !iter_empty(it); iter_next(it))
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
    node->__valid = 0;
    list->size -= 1;
    list->__has_invalid = TRUE;
    return data;
}


/**
 * Drop an item from a linked list by iterating through it
 */
void find_and_drop_item(LinkedList* list, void* data)
{
    ITER_LOOP(it, list)
    {
        if (iter_get(it) == data)
        {
            iter_drop_curr(it);
            break;
        }
    }
    iter_clean(it);
}


/**
 * Remove invalid nodes from a linked list.
 */
void remove_invalid(LinkedList* list)
{
    if (!list->__has_invalid) return;
    // Remove invalid nodes at the beginning
    while (list->head && !list->head->__valid)
    {
        Node* next_node = list->head->next;
        free(list->head);
        list->head = next_node; // There is no prev link to fix
    }
    
    Node* node = list->head;
    while (node)
    {
        if (!node->__valid)
        {
            // Fix Link
            Node* prev_node = node->prev;
            Node* next_node = node->next;
            if (prev_node)
                prev_node->next = next_node;
            if (next_node)
                next_node->prev = prev_node;
            
            free(node);
            node = next_node;
        }
        else
            node = node->next;
    }
    list->__has_invalid = FALSE;
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
 * Yield the current item from the iterator, and increment the pointer
 * to point to the next valid node if any (NULL otherwise).
 */
Node* get_and_incr(Iterator_LinkedList* it)
{
    Node* curr = it->curr;
    
    // Advance iterator until it finds a valid node, or
    // reaches the end of the list
    do {
        it->curr = it->curr->next;
    } while (it->curr && !it->curr->__valid);
    
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
    list->__references += 1;
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
    it->list->__references -= 1;
    // The last iterator refering to a list should call |remove_invalid|
    // to remove all invalid nodes
    if (it->list->__references == 0 && it->list->__has_invalid)
        remove_invalid(it->list);
    free(it);
}


/**
 * Advance the iterator.
 */
void iter_next(Iterator_LinkedList* it)
{
    if (it->curr)
        get_and_incr(it);
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
void* iter_drop_curr(Iterator_LinkedList* it)
{
    Node* to_be_dropped = get_and_incr(it);
    return drop_node(it->list, to_be_dropped);
}
