#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "linked-list.h"


#define TRUE 1
#define FALSE 0


void init_list(LinkedList* list)
{
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    list->__count = 0;
}

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

void drop_node(LinkedList* list, Node* node)
{
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
}

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

Iterator_LinkedList* iter(LinkedList* list)
{
    Iterator_LinkedList* it = malloc(sizeof(Iterator_LinkedList));
    it->list = list;
    it->curr = list->head;
    it->yielded = FALSE;
    return it;
}

int iter_empty(Iterator_LinkedList* it)
{
    return it->curr == NULL;
}

Iterator_LinkedList* iter_next(Iterator_LinkedList* it)
{
    if (!it->yielded)
        iter_yield(it);
    else
        it->yielded = FALSE;
    return it;
}

Node* iter_add(Iterator_LinkedList* it, void* data)
{
    return add_item(it->list, data);
}

/* Drop the current item */
void iter_drop(Iterator_LinkedList* it)
{
    Node* to_be_dropped = it->curr;
    iter_yield(it);
    drop_node(it->list, to_be_dropped);
}

void iter_drop_node(Iterator_LinkedList* it, Node* node)
{
    if (node == it->curr)
        iter_drop(it);
    else
        drop_node(it->list, node);
}

Node* iter_yield(Iterator_LinkedList* it)
{
    assert(!it->yielded);
    it->yielded = TRUE;
    Node* curr = it->curr;
    it->curr = it->curr->next;
    return curr;
}
