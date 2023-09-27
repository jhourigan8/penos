#ifndef QUEUE
#define QUEUE
#include <ucontext.h>
#include "scheduler.h"

typedef struct LinkedQueue Queue;
typedef struct ProcessControlBlock Pcb;

typedef enum ThreadPriority {
    LOW, MED, HIGH, INVALID
} Priority;

typedef struct QueueNode {
    struct QueueNode *prev;
    struct QueueNode *next;
    pid_t pid;
    pid_t ppid;
    int jid;
    int nice;
    int status;
    char* name;
    char* cmd;
    Pcb* pcb;
} Node;

typedef struct LinkedQueue {
    struct QueueNode *head;
    struct QueueNode *tail;
    int len;
} Queue;

typedef struct PrioNode {
    struct PrioNode *prev;
    struct PrioNode *next;
    Priority prio;
} PrioNode;

typedef struct PrioQueue {
    struct PrioNode *head;
    struct PrioNode *tail;
    int len;
} PrioQueue;

typedef enum JobStatus {
    RUN, BLOCK, STOP, ZOMB, ORPH
} Status;

typedef enum BlockedCause {
    SLEEP, WAIT, NOT_BLOCKED
} BlockedCause;

typedef enum SignaledStatus {
    NONE, STOPPED, RESTARTED
} Signal;

//PCB Definition
typedef struct ProcessControlBlock {
    pid_t PID;
    Pcb* parent;
    Queue* children;
    Queue* zombie_children;
    Queue* signals;
    ucontext_t* thread;
    Priority prio;
    pid_t wait_pid;
    Status status;
    BlockedCause bc;
    unsigned int blocked_ticks;
    const char* name;
    int fd_in;
    int fd_out;
    int signal;
    int child_signal;
    int no_changed_child;
    const char* waitedon;
} Pcb;

typedef struct PriorityQueues {
    Queue* LOW;
    Queue* MED;
    Queue* HIGH;
    Queue* LOT;
} Queues;

Queue* create_queue(void);
Node* create_node(pid_t id, Pcb* process);
void push_front(Queue* queue, Node* node);
void push_back(Queue* queue, Node* node);
Node* pop_front(Queue* queue);
Node* pop_back(Queue* queue);
Node* peek_front(Queue* queue);
Node* peek_back(Queue* queue);
Node* get_node(Queue* queue, pid_t pid);
Node* remove_pcb(Queue* queue, pid_t pid);


PrioQueue* prio_create_queue(void);
PrioNode* prio_create_node(Priority prio);
void prio_push_back(PrioQueue* queue, PrioNode* node);
Priority prio_get_priority(PrioQueue* queue, int counter);
#endif
