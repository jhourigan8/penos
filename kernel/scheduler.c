#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <ucontext.h>
#include <string.h>
#include <valgrind/valgrind.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include "queue.h"
#include "scheduler.h"
#include "shell_functions.h"

// List of Queues Used By Kernel
static Queue* queue_l;
static Queue* queue_m;
static Queue* queue_h;
static Queue* queue_blocked;
static Queue* queue_stopped;
static Queue* process_queue;
static Queue* queue_bg;
static PrioQueue* queue_lottery;

// State Variables Used to track important processes
// that are referenced by various OS processes
static Node* active_process = NULL;
static Node* shell = NULL;
static ucontext_t *active_context;
static unsigned int prev_ticks = 0;
static unsigned int all_ticks = 0;
static int alarm_triggered = 0;
static int context_switch_safe = 1;
int logfile;

// Major Contexts Used By The Project
static ucontext_t main_context;
static ucontext_t idle_context;
static ucontext_t scheduler_context;

//Demo Func
void hang(void);
void nohang(void);
void recur(void);

// Function Declarations
void idle(void);
void scheduler(void);
static void setTimer(void);
Pcb* k_process_create(Pcb *parent);
//static void exit_print(int signal);
void k_process_cleanup(Pcb* process);
void add_to_scheduler(Node* thread, Priority prio);
pid_t p_waitpid(pid_t pid, int *wstatus, int nohang);
pid_t p_spawn(void (*func)(), char *argv[], int fd0, int fd1);
static void makeContext(ucontext_t *ucp,  void (*func)(), int argC, char **arg1, int fd0, int fd1);
int k_process_kill(pid_t pid, int signal);

// Used to track scheduling counts for debugging
#define MAX_LINE_LENGTH 4096

// Gets the priority queue based on the priority level inputted //
Queue* get_priority_queue(Priority prio) {
    if (prio == LOW) {
        return queue_l;
    } else if (prio == MED) {
        return queue_m;
    } else if (prio == HIGH) {
        return queue_h;
    } else {
        return NULL;
    }
}

// Moves to the almost complete main context and terminates the context
// static void exit_print(int signal) {
//     //TODO: Add Closing Message
//     fprintf(stderr, "len: %d", queue_lottery->len);
//     setcontext(&main_context);
// }

// Idle Process
void idle(void) {
    sigset_t *mask = malloc(sizeof(sigset_t));
    sigemptyset(mask);
    sigsuspend(mask);
}

// Gets Nice Integer Value From Priority
int get_nice(Priority prio) {
    if (prio == HIGH) {
        return -1;
    } else if (prio == MED) {
        return 0;
    } else {
        return 1;
    }
}

// Sets Timer For Sig Alarm
static void setTimer(void)
{
    struct itimerval it;
    it.it_interval = (struct timeval) { .tv_usec = 10000 * 10 };
    it.it_value = it.it_interval;
    setitimer(ITIMER_REAL, &it, NULL);
}

// Updates Time Remaining for Sleeping Processes
void updateTicks() {
    Node* curr = queue_blocked->head;
    while(curr != NULL) {
        Node* next = curr -> next;
        if(curr->pcb->bc == SLEEP) {
            if(all_ticks - prev_ticks >= curr->pcb->blocked_ticks) {
                remove_pcb(queue_blocked, curr -> pcb -> PID);
                curr->pcb->status = RUN;
                curr->pcb->wait_pid = 0;
                curr->pcb->blocked_ticks = 0;
                curr->pcb->bc = NOT_BLOCKED;
                add_to_scheduler(curr, curr -> pcb -> prio);
                char buffer[256];
                sprintf(buffer, "[%d]\tUNBLOCKED\t%d\t%d\t%s\n", all_ticks, curr->pcb->PID, curr->pcb->prio, curr->pcb->name);
                write(logfile, buffer, strlen(buffer));
            } else {
                curr->pcb->blocked_ticks -= all_ticks - prev_ticks;
            }
        }
        curr = next;
    }
    prev_ticks = all_ticks;
}

// Handles Alarm Signals
static void alarmHandler(int signum)  {
    if (signum == SIGALRM) {
        all_ticks++;
        if(active_context != &scheduler_context) { 
            if(active_context != &scheduler_context) {
                if (active_context == &idle_context) {
                    setcontext(&scheduler_context);
                }
                if(context_switch_safe) {
                    swapcontext(active_context, &scheduler_context);
                } else {
                    alarm_triggered = 1;
                }
            }
        }
    }
}

// Sets Alarm Handler for Process
static void setAlarmHandler(void)
{
    struct sigaction *act = malloc(sizeof(struct sigaction));
    act->sa_handler = alarmHandler;
    act->sa_flags = SA_RESTART;
    sigfillset(&act->sa_mask);
    sigaction(SIGALRM, act, NULL);
}

// Sets Up Queue Lottery
void lottery_setup() {
    Priority prio = LOW;
    for (int i = 0; i < 150; i++) {
        if (i > 30 && i ) {prio = MED;}
        if (i > 75) {prio = HIGH;}
        PrioNode* ticket = prio_create_node(prio);
        prio_push_back(queue_lottery, ticket);
    }
}

// Gets Priority to Choose
Priority scheduler_lottery() {
    //start k+a change
    if (active_process != NULL && active_process->pcb->status != BLOCK) {
        Queue* tmp = get_priority_queue(active_process->pcb->prio);
        push_back(tmp, active_process);
        active_process = NULL;
    }
    if(queue_l -> len + queue_h -> len + queue_m -> len == 0) {
        if (active_process == NULL) {
            return INVALID;
        } else {
            return active_process->pcb->prio;
        }
    }
    //end k+a change
    int rand_int = random();
    int choice = rand_int % queue_lottery->len;
    Priority prio = prio_get_priority(queue_lottery, choice);
    Queue* use = get_priority_queue(prio);
    if (use->len == 0) {
        return scheduler_lottery();
    }
    return prio;
}

// ADDS THE PROCESS TO THE RIGHT PRIORITY QUEUE
void add_to_scheduler(Node* process, Priority prio) {
    Queue* use = get_priority_queue(prio);
    push_back(use, process);
}

// INITIALIZES THE VARIABLES UTILIZED BY THE SCHEDULER
void init_scheduler(char* logname) {
    //initializes all the queues
    queue_l = create_queue();
    queue_m = create_queue();
    queue_h = create_queue();
    queue_blocked = create_queue();
    queue_stopped = create_queue();
    process_queue = create_queue();
    queue_bg = create_queue();
    queue_lottery = prio_create_queue();
    logfile = open(logname, O_APPEND | O_TRUNC | O_CREAT | O_RDWR, 0644);
    srandom(time(NULL));
    lottery_setup();
    makeContext(&scheduler_context, scheduler, 0, 0, STDIN_FILENO, STDERR_FILENO);
    makeContext(&idle_context, idle, 0, NULL, STDIN_FILENO, STDERR_FILENO);
    setAlarmHandler();
    setTimer();
}

// ALLOCATES MEMORY THAT IS USED EACH CONTEXT 
static void setStack(stack_t *stack)
{
    void *sp = malloc(SIGSTKSZ * 100);
    VALGRIND_STACK_REGISTER(sp, sp + SIGSTKSZ * 100);
    *stack = (stack_t) { .ss_sp = sp, .ss_size = SIGSTKSZ * 100};
}

// MAKE CONTEXT MAKES THE CONTEXT FOR A PARTICULAR FUNCTION
static void makeContext(ucontext_t *ucp,  void (*func)(), int argC, char **arg1, int fd0, int fd1)
{
    getcontext(ucp);
    sigemptyset(&ucp->uc_sigmask);
    setStack(&ucp->uc_stack);
    ucp->uc_link = func == scheduler ? NULL : &scheduler_context;
    //TO DO:
    if (argC > 0)
        makecontext(ucp, func, 3, arg1, fd0, fd1);
    else
        makecontext(ucp, func, 0);
}

// Actually Switches the Context
void context_set(Priority prio) {
    if(prio == INVALID) {
        active_process = NULL;
        active_context = &idle_context;
        alarm_triggered = 0;
        context_switch_safe = 1;
        char buffer[256];
        sprintf(buffer, "[%d]\tSCHEDULE\t%d\t%d\t%s\n", all_ticks, -1, -1, "IDLE");
        write(logfile, buffer, strlen(buffer));
        setcontext(&idle_context);
    } else {
        Queue* use = get_priority_queue(prio);
        Node* curr;
        if (use->len == 0) {
            curr = active_process;
        } else {
            curr = pop_front(use);
        }
        active_process = curr;
        active_context = (curr->pcb->thread);
        char* buffer = malloc(256 * sizeof(char));
        sprintf(buffer, "[%d]\tSCHEDULE\t%d\t%d\t%s\n", all_ticks, active_process->pcb->PID, get_nice(active_process->pcb->prio), active_process->pcb->name);
        write(logfile, buffer, strlen(buffer));
        alarm_triggered = 0;
        context_switch_safe = 1;
        setcontext(active_context);
    }
}

// Main Control Flow Function Guiding Scheduling
void scheduler(void) {
    //TODO Handle zombie processes
    updateTicks();
    Priority winner = scheduler_lottery();
    if (active_process != NULL && active_process -> pcb -> status == RUN) {
        add_to_scheduler(active_process, active_process -> pcb -> prio);
    }
    context_set(winner);
}

void setup(void (*func)()) {
    active_process = NULL;
    active_context = NULL;
    char **args = malloc(sizeof(char*));
    args[0] = "shell";
    pid_t shell_pid = p_spawn(func, args, -1, -1);
    shell = get_node(process_queue, shell_pid);
    p_nice(shell_pid, -1);
}

void p_setup_scheduler(void (*func)(), char* logname) {
    init_scheduler(logname);
    setup(func);
    swapcontext(&main_context, &scheduler_context);
}

// Switch Contexts If Alarm Triggered Mid User Level Call
void checkAlarmTriggered() {
    context_switch_safe = 1;
    if(alarm_triggered) {
        alarm_triggered = 0;
        swapcontext(active_context, &scheduler_context);
    }
}

void k_end(void) {
    setcontext(&main_context);
}

void p_logout(void) {
    k_process_kill(1, 0);
    k_end();
}

// Creates a PCB's memory space and puts PCB in it
Pcb* k_process_create(Pcb *parent) {
    static pid_t pid = 1;
    Pcb* process = (Pcb*) malloc(sizeof(struct ProcessControlBlock)*16*16);
    process->PID = pid;
    process->parent = active_process == NULL ? NULL : active_process -> pcb;
    process->children = create_queue();
    process->zombie_children = create_queue();
    process->signals = create_queue();
    process->prio = MED;
    process->status = RUN;
    process->blocked_ticks = 0;
    process->wait_pid = 0;
    process->signal = -1;
    process->child_signal = -1;
    process->bc = NOT_BLOCKED;
    process->no_changed_child = 1;
    process->waitedon = NULL;
    pid += 1;
    return process;
}

char** p_ps(void) {
    Node* curr = process_queue->head;
    char** out = malloc(sizeof(char*)*process_queue->len+1);
    int i = 0;
    while (curr != NULL) {
        out[i] = malloc(sizeof(char)*256);
        char status = 0;
        Pcb* curr_pcb = curr->pcb;
        Pcb* parent_pcb = curr->pcb->parent;
        int pid = curr_pcb->PID;
        int ppid = parent_pcb == NULL ? 0 : parent_pcb->PID;
        int prio = get_nice(curr_pcb->prio);
        int stat = (int) curr_pcb->status;
        if (stat == RUN) {
            status = 'R';
        } else if (stat == BLOCK) {
            status = 'B';
        } else if (stat == STOP) {
            status = 'S';
        } else if (stat == ZOMB) {
            status = 'Z';
        } else if (stat == ORPH) {
            status = 'O';
        }
        //fprintf(stderr, "PID:%d\tPPID:%d\tPriority:%d\tStatus:%c\tName:%s\n", pid, ppid, prio, status, curr->name);
        sprintf(out[i], "PID:%d\tPPID:%d\tPriority:%d\tStatus:%c\tName:%s\n", pid, ppid, prio, status, curr->name);
        i++;
        curr = curr->next;
    }
    out[i] = "\0";
    return out;
}

// Process System Call to Create New Process
pid_t p_spawn(void (*func)(), char *argv[], int fd0, int fd1) {
    //Creates Thread
    context_switch_safe = 0;
    ucontext_t* context = (ucontext_t*) malloc(sizeof(ucontext_t));

    //TODO Shell: Add Different Handling Based on Command
    makeContext(context, func, 3, argv, fd0, fd1);

    //Creates Queue Nodes And Adds to Queues
    Pcb* process_block;
    if (active_process == NULL) {
        process_block = k_process_create(NULL);
    } else {
        process_block = k_process_create(active_process->pcb);
    }
    process_block -> thread = context;
    process_block->name = argv[0];
    Node* queue_node1 = create_node(process_block -> PID, process_block);
    Node* queue_node2 = create_node(process_block -> PID, process_block);
    Node* queue_node3 = create_node(process_block -> PID, process_block);
    queue_node1->name = malloc(sizeof(char)*256);
    sprintf(queue_node1->name, "%s", argv[0]);
    push_back(process_queue, queue_node1);
    if(active_process != NULL) {
        push_back(active_process->pcb->children, queue_node2);
    }
    push_back(get_priority_queue(queue_node3->pcb->prio), queue_node3);

    //Logs Generation Message
    char buffer[256];
    sprintf(buffer, "[%d]\tCREATE\t%d\t%d\t%s\n", all_ticks, process_block->PID, process_block->prio, process_block->name);
    write(logfile, buffer, strlen(buffer));
    
    //Checks if Alarm Was Triggered and Switches if so
    checkAlarmTriggered();
    return process_block->PID;
}


// Alters Process Scheduling Priority
int p_nice(pid_t pid, int priority) {
    context_switch_safe = 0;
    Node* process_node = get_node(process_queue, pid);
    if(process_node == NULL) {
        checkAlarmTriggered();
        return -2;
    }
    Pcb* pcb = process_node->pcb;
    
    Priority new_prio;
    if(priority == -1) {
        new_prio = HIGH;
    } else if (priority == 0) {
        new_prio = MED;
    } else {
        new_prio = LOW;
    }
    if(pcb -> prio == new_prio) {
        checkAlarmTriggered();
        return -2;
    } else {
        char buffer[256];
        sprintf(buffer, "[%d]\tNICE\t%d\t%d\t%d\t%s\n", all_ticks, pcb->PID, get_nice(pcb->prio), get_nice(new_prio), pcb->name);
        write(logfile, buffer, strlen(buffer));
        if (pcb -> status != RUN) {
            pcb -> prio = new_prio;
        } else {
            pcb -> prio = new_prio;
            Node* node = remove_pcb(get_priority_queue(pcb->prio), pcb->PID);
            if(node != NULL) {
                push_back(get_priority_queue(node->pcb->prio), node);
            }
        }
    }
    checkAlarmTriggered();
    return get_nice(new_prio);
}

//TODO: Test thoroughly + Add Cleanup on Popped Nodes
//TODO: make sure return 0
//TODO: review waitpid handling in linux (check if no child cases + other cases make sense)
pid_t p_waitpid(pid_t pid, int *wstatus, int nohang) {
    context_switch_safe = 0;
    Node* curr = active_process;
    if(active_process -> pcb -> children -> len + active_process -> pcb -> zombie_children -> len == 0) {
        //fprintf(stderr, "HERE 1");
        if(wstatus != NULL)
            *wstatus = -2;
        return -1;
    }
    if(pid == -1) {
         if(curr -> pcb -> zombie_children -> head != NULL) {
            Node* zomb = pop_front(curr -> pcb -> zombie_children);
            pid = zomb -> pcb -> PID;
            if(wstatus != NULL)
                *wstatus = zomb -> pcb -> signal;
            char buffer[256];
            sprintf(buffer, "[%d]\tWAITED\t%d\t%d\t%s\n", all_ticks, zomb->pcb->PID, zomb->pcb->prio, zomb->pcb->name);
            write(logfile, buffer, strlen(buffer));
            k_process_cleanup(zomb->pcb);
            free(zomb);
            curr -> pcb -> wait_pid = 0;
            curr -> pcb -> no_changed_child = 0;
            checkAlarmTriggered();
            return pid;
        } else if (curr -> pcb -> signals -> head != NULL) {
            Node* stop = pop_front(curr -> pcb -> signals);
            if(wstatus != NULL)
                *wstatus = stop -> pcb -> signal;
            char buffer[256];
            sprintf(buffer, "[%d]\tWAITED\t%d\t%d\t%s\n", all_ticks, stop->pcb->PID, stop->pcb->prio, stop->pcb->name);
            write(logfile, buffer, strlen(buffer));
            stop -> pcb -> signal = -1;
            pid = stop -> pcb -> PID;
            free(stop);
            curr -> pcb -> wait_pid = 0;
            curr -> pcb -> no_changed_child = 0;
            checkAlarmTriggered();
            return pid;
        } else {
            if (nohang) {
                if(wstatus != NULL)
                    *wstatus = -1;
                curr -> pcb -> wait_pid = 0;
                checkAlarmTriggered();
                return 0;
            } else {
                curr -> pcb -> status = BLOCK;
                curr -> pcb -> blocked_ticks = 0;
                curr -> pcb -> wait_pid = pid;
                curr -> pcb -> bc = WAIT;
                push_back(queue_blocked, curr);

                char buffer[256];
                sprintf(buffer, "[%d]\tBLOCKED\t%d\t%d\t%s\n", all_ticks, active_process->pcb->PID, get_nice(active_process->pcb->prio), active_process->pcb->name);
                write(logfile, buffer, strlen(buffer));

                active_process = NULL;
                swapcontext(active_context, &scheduler_context);
                context_switch_safe = 0;
                sprintf(buffer, "[%d]\tWAITED\t%d\t%d\t%s\n", all_ticks, active_process->pcb->PID, active_process->pcb->prio, active_process->pcb->waitedon);
                active_process->pcb->waitedon = NULL;
                write(logfile, buffer, strlen(buffer));
                curr -> pcb -> no_changed_child = 0;
                if(wstatus != NULL)
                    *wstatus = curr -> pcb -> child_signal;
                curr->pcb->child_signal = -1;
                pid = curr -> pcb -> wait_pid;
                curr -> pcb -> wait_pid = 0;
                checkAlarmTriggered();
                return pid;
            }
        }
    } else {
        if(get_node(curr -> pcb -> zombie_children, pid) != NULL) {
            Node* zomb = remove_pcb(curr -> pcb -> zombie_children, pid);
            pid = zomb -> pcb -> PID;
            if(wstatus != NULL)
                *wstatus = zomb -> pcb -> signal;
            k_process_cleanup(zomb->pcb);
            free(zomb);
            curr -> pcb -> wait_pid = 0;
            curr -> pcb -> no_changed_child = 0;
            checkAlarmTriggered();
            return zomb -> pcb -> PID;
        } else if(get_node(curr -> pcb -> signals, pid) != NULL) {
            Node* stop = remove_pcb(curr -> pcb -> signals, pid);
            if(wstatus != NULL)
                *wstatus = stop -> pcb -> signal;
            stop -> pcb -> signal = -1;
            pid = stop -> pcb -> PID;
            free(stop);
            curr -> pcb -> wait_pid = 0;
            curr -> pcb -> no_changed_child = 0;
            checkAlarmTriggered();
            return pid;
        } else {
            if(nohang) {
                if(wstatus != NULL)
                    *wstatus = -1;
                curr -> pcb -> wait_pid = 0;
                checkAlarmTriggered();
                return 0;
            } else {
                curr -> pcb -> status = BLOCK;
                curr -> pcb -> wait_pid = pid;
                curr -> pcb -> blocked_ticks = 0;
                curr -> pcb -> bc = WAIT;
                push_back(queue_blocked, curr);

                char buffer[256];
                sprintf(buffer, "[%d]\tBLOCK\t%d\t%d\t%s\n", all_ticks, active_process->pcb->PID, active_process->pcb->prio, active_process->pcb->name);
                write(logfile, buffer, strlen(buffer));

                active_process = NULL;
                swapcontext(active_context, &scheduler_context);
                context_switch_safe = 0;
                sprintf(buffer, "[%d]\tWAITED\t%d\t%d\t%s\n", all_ticks, active_process->pcb->PID, active_process->pcb->prio, active_process->pcb->waitedon);
                active_process->pcb->waitedon = NULL;
                write(logfile, buffer, strlen(buffer));
                curr -> pcb -> no_changed_child = 0;
                if(wstatus != NULL)
                    *wstatus = curr -> pcb -> child_signal;
                curr->pcb->child_signal = -1;
                curr -> pcb -> wait_pid = 0;
                checkAlarmTriggered();
                return pid;
            }
        }
    }
}

void kill_orphans(Node* process, int isChild) {
    while(process -> pcb -> children -> len > 0) {
        Node* child = pop_front(process -> pcb -> children);
        kill_orphans(child, 1);
    }

    while(process -> pcb -> zombie_children -> len > 0) {
        Node* child = pop_front(process -> pcb -> zombie_children);
        Pcb* child_process = child -> pcb;
        char buffer[256];
        //TODO: Check this orphaned message
        sprintf(buffer, "[%d]\tORPHANED\t%d\t%d\t%s\n", all_ticks, child_process->PID, child_process->prio, child_process->name);
        write(logfile, buffer, strlen(buffer));
        free(child);
        k_process_cleanup(child_process);
    }
    if(isChild) {
        //TODO: Check for what happens if child kills parent
        Node* runningNode;
        if (process->pcb->status == BLOCK) {
            runningNode = remove_pcb(queue_blocked, process -> pcb -> PID);
        } else if (process -> pcb -> status == STOP) {
            runningNode = remove_pcb(queue_stopped, process -> pcb -> PID);
        } else {
            runningNode = remove_pcb(get_priority_queue(process->pcb->prio), process -> pcb -> PID);
        }
        if(runningNode != NULL) {
            free(runningNode);
        }
        Pcb* child_process = process -> pcb;
        char buffer[256];
        sprintf(buffer, "[%d]\tORPHANED\t%d\t%d\t%s\n", all_ticks, child_process->PID, child_process->prio, child_process->name);
        write(logfile, buffer, strlen(buffer));
        free(process);
        k_process_cleanup(child_process);  
    }
}


//Double Check Flow W/ WaitPID

//TODO: make sure to update signal (both process signal + parent child signal) on kill
//TODO: make sure to update wait pid if unblock
//TODO: Handle zombie process having null status
int k_process_kill(pid_t pid, int signal) {
    Node* node = get_node(process_queue, pid);
    if(signal == 0) {
        if(node -> pcb -> parent == NULL) {
            exit(400);
        } 

        Pcb* parent = node->pcb->parent;
        if(get_node(parent->signals, pid) != NULL) {
            free(remove_pcb(parent->signals, pid));
        }

        Node* running_node;
        if (node -> pcb -> status == STOP) {
            running_node = remove_pcb(queue_stopped, pid);
        } else if (node -> pcb -> status == BLOCK) {
            running_node = remove_pcb(queue_blocked, pid);
        } else {
            if (node->pcb->PID == active_process->pcb->PID) {
                running_node = active_process;
                active_process = NULL;
            } else {
                running_node = remove_pcb(get_priority_queue(node -> pcb -> prio), node -> pcb -> PID);
            }
        }
        free(running_node);
        Node* zombie_node = remove_pcb(parent -> children, node -> pcb -> PID);
        zombie_node -> pcb -> status = ZOMB;
        
        
        char buffer[256];
        sprintf(buffer, "[%d]\tZOMBIE\t%d\t%d\t%s\n", all_ticks, zombie_node->pcb->PID, zombie_node->pcb->prio, zombie_node->pcb->name);
        write(logfile, buffer, strlen(buffer));
        kill_orphans(zombie_node, 0);

        if (parent -> status == BLOCK && parent -> bc == WAIT && (parent -> wait_pid == -1 || parent -> wait_pid == node -> pcb -> PID)) {
            char buffer[256];
            sprintf(buffer, "[%d]\tUNBLOCKED\t%d\t%d\t%s\n", all_ticks, parent->PID, parent->prio, parent->name);
            write(logfile, buffer, strlen(buffer));

            parent-> blocked_ticks = 0;
            parent -> bc = NOT_BLOCKED;
            parent -> status = RUN;
            parent -> child_signal = zombie_node -> pcb -> signal;
            parent -> wait_pid = zombie_node -> pcb -> PID;
            parent -> no_changed_child = 0;
            parent->waitedon = zombie_node->pcb->name;

            add_to_scheduler(remove_pcb(queue_blocked, parent -> PID), parent -> prio);
            k_process_cleanup(zombie_node->pcb);
            free(zombie_node);
        } else {
            push_back(parent -> zombie_children, zombie_node);
        }
        return 0;
    } else if (signal == 1) {
        if(node -> pcb -> parent == NULL) {
            exit(400);
        } 

        Node* process;
        Node* sig;
        Pcb* parent = node -> pcb -> parent;

        if (node -> pcb -> status == BLOCK) {
            process = remove_pcb(queue_blocked, pid);
        } else {
            if(active_process != NULL && active_process -> pcb -> PID == pid) {
                process = active_process;
                active_process = NULL;
            } else {
                process = remove_pcb(get_priority_queue(node->pcb->prio), pid);
            }
            process -> pcb -> bc = NOT_BLOCKED;
        }

        //TODO: Log Stopped Process
        process->pcb->status = STOP;
        push_back(queue_stopped, process);
        if(get_node(parent -> signals, process -> pcb -> PID) == NULL) {
            if (parent -> status == BLOCK && parent -> bc == WAIT && (parent -> wait_pid == -1 || parent -> wait_pid == pid)) {
                char buffer[256];
                sprintf(buffer, "[%d]\tUNBLOCKED\t%d\t%d\t%s\n", all_ticks, parent->PID, parent->prio, parent->name);
                write(logfile, buffer, strlen(buffer));

                parent -> bc = NOT_BLOCKED;
                parent-> blocked_ticks = 0;
                parent -> status = RUN;
                parent -> child_signal = process -> pcb -> signal;
                parent -> wait_pid = process -> pcb -> PID;
                parent -> no_changed_child = 0;
                parent->waitedon = process->name;
                add_to_scheduler(remove_pcb(queue_blocked, parent -> PID), parent -> prio);
            } else {
                sig = create_node(node -> pcb -> PID, node -> pcb);
                push_back(sig->pcb->parent->signals, sig);
            }
        }
        return 0;
    } else {
        if(node -> pcb -> parent == NULL) {
            exit(400);
        } 
        
        Node* sig;
        Node* process = remove_pcb(queue_stopped, pid);
        Pcb* parent = node -> pcb -> parent;

        if(process -> pcb -> bc != NOT_BLOCKED) {
            process->pcb->status = BLOCK;
            push_back(queue_blocked, process);
            //TODO: Log Process As Blocked Again
            //TODO: Add handling for when blocked process should have been unbloked by signal sent
        } else {
            process->pcb->status = RUN;
            push_back(get_priority_queue(process->pcb->prio), process);
            //TODO: Log Restart Process
        }
        
        if(get_node(parent -> signals, node -> pcb -> PID) == NULL) {
            if (parent -> status == BLOCK && parent -> bc == WAIT && (parent -> wait_pid == -1 || parent -> wait_pid == pid)) {
                char buffer[256];
                sprintf(buffer, "[%d]\tUNBLOCKED\t%d\t%d\t%s\n", all_ticks, parent->PID, parent->prio, parent->name);
                write(logfile, buffer, strlen(buffer));

                parent -> bc = NOT_BLOCKED;
                parent-> blocked_ticks = 0;
                parent -> status = RUN;
                parent -> child_signal = process -> pcb -> signal;
                parent -> wait_pid = process -> pcb -> PID;
                parent -> no_changed_child = 0;
                parent->waitedon = process->name;
                add_to_scheduler(remove_pcb(queue_blocked, parent -> PID), parent -> prio);
            } else {
                sig = create_node(node -> pcb -> PID, node -> pcb);
                push_back(sig->pcb->parent->signals, sig);
            }
        }
        return 0;
    }
        
}

int p_kill(pid_t pid, int sig){
    context_switch_safe = 0;
    Node* signaled_process = get_node(process_queue, pid);
    if(signaled_process == NULL) {
        return -1;
    }
    if(signaled_process->pcb->status == ZOMB) {
        checkAlarmTriggered();
        return -1;
    } else if(signaled_process->pcb->status == STOP && sig == 1) {
        checkAlarmTriggered();
        return -1;
    } else if(signaled_process->pcb->status != STOP && sig == 2){
        checkAlarmTriggered();
        return -1;
    } else {
        signaled_process->pcb->signal = sig;
        char buffer[256];
        sprintf(buffer, "[%d]\tSIGNALED\t%d\t%d\t%s\n", all_ticks, signaled_process->pcb->PID, signaled_process->pcb->prio, signaled_process->pcb->name);
        write(logfile, buffer, strlen(buffer));
        int res = k_process_kill(pid, sig);
        checkAlarmTriggered();
        return res;
    }
}

void k_process_cleanup(Pcb* process) {
    Node* process_node = remove_pcb(process_queue, process->PID);
    free(process_node);
}

void p_exit() {
    context_switch_safe = 0;
    if(active_process != NULL) {
        char buffer[256];
        sprintf(buffer, "[%d]\tEXITED\t%d\t%d\t%s\n", all_ticks, active_process->pcb->PID, active_process->pcb->prio, active_process->pcb->name);
        write(logfile, buffer, strlen(buffer));

        active_process -> pcb -> signal = 3;
        k_process_kill(active_process->pid, 0);
        setcontext(&scheduler_context);
    } else {
        checkAlarmTriggered();
    }
}

void p_sleep(unsigned int ticks) {
    context_switch_safe = 0;
    if(ticks != 0 && active_process != NULL) {
        char buffer[256];
        sprintf(buffer, "[%d]\tBLOCKED\t%d\t%d\t%s\n", all_ticks, active_process->pcb->PID, active_process->pcb->prio, active_process->pcb->name);
        write(logfile, buffer, strlen(buffer));
        active_process->pcb->status = BLOCK;
        active_process->pcb->wait_pid = 0;
        active_process->pcb->blocked_ticks = ticks;
        active_process->pcb->bc = SLEEP;
        push_back(queue_blocked, active_process);
        active_process = NULL;
        swapcontext(active_context, &scheduler_context);
    }
    checkAlarmTriggered();
}

static void nap(void)
{
    usleep(10000); // 10 milliseconds
    p_exit();
}

static void spawn(int nohang)
{
    char name[] = "child_0";
    char *argv[] = { name, NULL };
    int pid = 0;
    // Spawn 10 nappers named child_0 through child_9.
    for (int i = 0; i < 10; i++) {
        argv[0][sizeof name - 2] = '0' + i;
        const int id = p_spawn(nap, argv, -1, -1);
        if (i == 0)
        pid = id;
        dprintf(STDERR_FILENO, "%s was spawned\n", *argv);
    }
    usleep(100000);
    // Wait on all children.
    while (1) {
        const int cpid = p_waitpid(-1, NULL, nohang);
        if (cpid < 0) // no more waitable children (if block-waiting) or error
            break;
        // polling if nonblocking wait and no waitable children yet
        if (nohang && cpid == 0) {
            usleep(90000); // 90 milliseconds
            continue;
        }
        dprintf(STDERR_FILENO, "child_%d was reaped\n", cpid - pid);
    }
}
/*
* The function below recursively spawns itself 26 times and names the spawned
* processes Gen_A through Gen_Z. Each process is block-waited by its parent.
*/
static void spawn_r(void)
{
    static int i = 0;
    int pid = 0;
    char name[] = "Gen_A";
    char *argv[] = { name, NULL };
    if (i < 26) {
        argv[0][sizeof name - 2] = 'A' + i++;
        pid = p_spawn(spawn_r, argv, -1, -1);
        dprintf(STDERR_FILENO, "%s was spawned\n", *argv);
        usleep(10000); // 10 milliseconds
    } 
    if (pid > 0 && pid == p_waitpid(pid, NULL, 0))
        dprintf(STDERR_FILENO, "%s was reaped\n", *argv);
    p_exit();
}
/******************************************************************************
* *
* Add commands hang, nohang, and recur to the shell as built-in subroutines *
* which call the following functions, respectively. *
* *
******************************************************************************/
void hang(void)
{
    spawn(0);
    p_exit();
}
void nohang(void)
{
    spawn(1);
    p_exit();
}
void recur(void)
{
    spawn_r();
    
    p_exit();
}
