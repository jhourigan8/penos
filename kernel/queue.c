#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdint.h>
#include "queue.h"

#define NULL ((void*)0)

//creates queue and mallocs needed space
Queue* create_queue(void) {
    Queue* queue = (Queue*) malloc(64*sizeof(Node*)*sizeof(Queue*)*16);
    queue->head = NULL;
    queue->tail = NULL;
    queue->len = 0;
    return queue;
}

//creates queue node and needed space
Node* create_node(pid_t id, Pcb* process) {
    Node* node = (Node*) malloc(sizeof(struct QueueNode)*16*16);
    node -> pcb = process;
    node -> pid = id;
    return node;
}

//pushes node to the front of the deque
void push_front(Queue* queue, Node* node) {
    if (node == NULL) { return; }
    Node* currhead = queue->head;
    node->prev = NULL;
    node->next = queue->head;
    if (currhead != NULL) {
        currhead->prev = node;
    } else {
        queue->tail = node;
    }
    queue->head = node;
    queue->len++;
}

// adds node to the end of the queu
void push_back(Queue* queue, Node* node) {
    if (node == NULL) { return; }
    Node* currtail = queue->tail;
    node->next = NULL;
    node->prev = queue->tail;
    if (currtail != NULL) {
        currtail->next = node;
        node->jid = currtail->jid + 1;
        //fprintf(stderr, "HERE");
    } else {
        node->jid = 1;
        queue->head = node;
    }
    queue->tail = node;
    if(queue->head->next == queue->tail) {
        //fprintf(stderr, "WTF");
        //fprintf(stderr, "%d\n%d\n", queue->head->pid, queue->head->next->pid);
    }
    queue->len += 1;
}

//pops the element at the front of the array
Node* pop_front(Queue* queue) {
    if (queue->head != NULL) {
        Node* old_head = queue->head;
        if (queue->head == queue->tail) {
            queue->head = NULL;
            queue->tail = NULL;
        } else {
            queue->head = old_head->next;
            queue->head->prev = NULL;
        }
        queue->len--;
        old_head->next = NULL;
        return old_head; 
    } else { 
        return NULL; 
    }
}
//pops the element at the back of the array
Node* pop_back(Queue* queue) {
    if (queue->tail != NULL) {
        Node* old_tail = queue->tail;
        if (queue->head == queue->tail) {
            queue->head = NULL;
            queue->tail = NULL;
        } else {
            queue->tail = old_tail->prev;
            queue->tail->next = NULL;
        }
        queue->len--;
        old_tail->prev = NULL;
        return old_tail; 
    } else { 
        return NULL; 
    }
}
//peeks at the pid at the front of the queue
Node* peek_front(Queue* queue) {
    return queue->head;
}
//peeks at the pid at the back of the queue
Node* peek_back(Queue* queue) {
    return queue->tail;
}

//removes a pcb from the queue
Node* remove_pcb(Queue* queue, pid_t pid) {
    Node* curr = queue->head;
    while (curr != NULL && curr -> pcb -> PID != pid) { curr = curr->next; }
    if (curr == NULL) { return NULL; }
    if (curr == queue->head) { return pop_front(queue); }
    if (curr == queue->tail) { return pop_back(queue); }
    Node* prev = curr->prev;
    Node* next = curr->next;
    prev->next = next;
    next->prev = prev;
    queue->len--;
    return curr;
}

//checks whether a process is stopped, running, or finished in the queue
Node* get_node(Queue* queue, pid_t pid) {
    Node* curr = queue->head;
    //fprintf(stderr, "get_node: pid1: %d, pid2: %d", queue->head->pcb->PID, queue->tail->pcb->PID);
    //fprintf(stderr, "is prev right: %d\n", queue->tail->prev == queue->head);
    while (curr != NULL) {
        if (curr->pcb->PID == pid) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

//frees the queue and frees all the space malloc-ed to the queue
void free_queue(Queue* queue) {
    Node* curr = queue->head;
    while (curr != NULL) {
        Node* curr2 = curr;
        curr = curr->next;
        free(curr2);
    }
    free(queue);
}


PrioQueue* prio_create_queue(void) {
    PrioQueue* queue = (PrioQueue*) malloc(sizeof(struct PrioQueue));
    queue->head = NULL;
    queue->tail = NULL;
    queue->len = 0;
    return queue;
}

PrioNode* prio_create_node(Priority prio) {
    PrioNode* node = (PrioNode*) malloc(sizeof(struct PrioNode));
    node -> prio = prio;
    return node;
}
void prio_push_back(PrioQueue* queue, PrioNode* node) {
    if (node == NULL) { return; }
    PrioNode* currtail = queue->tail;
    node->next = NULL;
    node->prev = queue->tail;
    if (currtail != NULL) {
        currtail->next = node;
    } else {
        queue->head = node;
    }
    queue->tail = node;
    queue->len += 1;
}

Priority prio_get_priority(PrioQueue* queue, int counter) {
    PrioNode* curr = queue->head;
    int i = 0;
    while (i < counter && curr != NULL) {
        curr = curr->next; 
        i++;
    }
    if (i < counter) { return INVALID; }
    return curr->prio;
}