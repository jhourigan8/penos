#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "kernel/scheduler.h"
#include "kernel/shell_functions.h"
#include "kernel/queue.h"
#include "fs/syscalls.h"
#include "error.h"

#define MAX_LINE_LENGTH 4096
Node* create_bg_node(pid_t pid);
static Queue* queue_bg;

volatile pid_t pid_global = -1;
volatile int nice_global = -1;
volatile int back = -1;
char* name;
char* pcmd_g;

int curr_back(void);

void signal_handler(int signum) {
    if (signum == SIGINT) {
        int killed = p_kill(pid_global, 0);
        f_write(STDERR_FILENO, "\n", 1);
        if(killed) {
            f_write(STDERR_FILENO, PROMPT, 3);
        }
        back = curr_back();
    } else if (signum == SIGTSTP) {
        Node* stopped = create_bg_node(pid_global);
        stopped->status = 1;
        stopped->name = name;
        stopped->cmd = pcmd_g;
        push_back(queue_bg, stopped);
        p_kill(pid_global, 1);
        f_write(STDERR_FILENO, "\n", 1);
        back = curr_back();
    }
}

char* get_name(int pid) {
    Node* curr = queue_bg->tail;
    while (curr != NULL) {
        if (curr->pid == pid) {
            return curr->name;
        }
        curr = curr->prev; 
    }
    return queue_bg->tail == NULL ? NULL : queue_bg->tail->name;
}

Node* create_bg_node(pid_t pid) {
    Node* node = create_node(pid, NULL);
    node->ppid = 1;
    node->nice = 0;
    node->status = 0;
    return node;
}

void echo_func(char *argv[], int fdin, int fdout) {
    int i = 1;
    while (strcmp(argv[i], "\0")) {
        int len = strlen(argv[i])+1;
        argv[i][strlen(argv[i])] =  (strcmp(argv[i+1], "\0")) ? ' ' : '\n'; 
        f_write(fdout, argv[i], len);
        i++;
    }
    f_close(fdout);
    p_exit();
}

void kill_fn(char* argv[]) {
    int sign = 0;
    int start = 1;
    char* signal = argv[1];
    if (!strcmp(signal, "-term")) {
        start = 1;
    } else if (!strcmp(signal, "-stop")) {
        sign = 1;
        start = 2;
    } else if (!strcmp(signal, "-cont")) {
        sign = 2;
        start = 2;
    }
    while (strcmp("\0", argv[start])) {
        char* name = get_name(atoi(argv[start]));
        if (name != NULL && !strcmp("cat", name)) {
            p_kill(atoi(argv[start]), 0);
            start++;
            continue;
        }
        p_kill((pid_t) atoi(argv[start]), sign);
        start++;
    }
    p_exit();
}

//attributed to sched_demo
void cat_fn(char *argv[], int fdin, int fdout) {
    const int size = 1;
    char buffer[size];

    if (fdin == STDIN_FILENO && strcmp(argv[1], "\0")) {
        int fdx = f_open(abs_path(argv[1]), READ);
        fdin = (fdx == -1) ? STDIN_FILENO : fdx;
    }

    for (;;) {
        int n = f_read(fdin, size, buffer);

        if (n > 0) {
            f_write(fdout, buffer, n);
        } else {
            break;
        }
    }
    f_close(fdin);
    p_exit();
}

void ps_fn(char *argv[], int fdin, int fdout) {
    char** out = p_ps();
    int i = 0;
    while (strcmp(out[i], "\0")) {
        f_write(fdout, out[i], strlen(out[i])+1);
        i++;
    }
    p_exit();
}

void touch_fn(char* argv[]) {
    f_touch(argv);
    p_exit();
}

void mv_fn(char* argv[]) {
    f_mv(argv);
    p_exit();
}

void cp_fn(char* argv[]) {
    f_cp(argv);
    p_exit();
}

void rm_fn(char* argv[]) {
    f_rm(argv);
    p_exit();
}

void chmod_fn(char* argv[]) {
    f_chmod(argv);
    p_exit();
}

void cd_fn(char* argv[]) {
    f_cd(argv);
    p_exit();
}

void mkdir_fn(char* argv[]) {
    f_mkdir(argv);
    p_exit();
}

void rmdir_fn(char* argv[]) {
    f_rmdir(argv);
    p_exit();
}

void pwd_fn(char* argv[], int fdin, int fdout) {
    char* out = f_pwd(argv);
    f_write(fdout, out, strlen(out) + 1);
    p_exit();
}

void ln_fn(char* argv[]) {
    f_ln(argv);
    p_exit();
}

void ls_fn(char* argv[], int fdin, int fdout) {
    char** out = NULL;
    if (strcmp(argv[1], "\0")) {
        out = f_ls(argv[1]);
    } else {
        out = f_ls(NULL);
    }
    int i = 0;
    if (out) {
        while (strcmp(out[i], "\0")) {
            f_write(fdout, out[i], strlen(out[i])+1);
            i++;
        }
    }
    p_exit();
}

char* get_abs_path(char* filename) {
    return abs_path(filename);
}

//removes a job from a queue given the job's pgid as pid
Node* remove_job(Queue* queue, int pid) {
    if (queue == NULL) {return NULL;}
    Node* curr = queue->head;
    while (curr != NULL && curr->pid != pid) { curr = curr->next; }
    if (curr == NULL) { return NULL; }
    if (curr == queue->head) { return pop_front(queue); }
    if (curr == queue->tail) { return pop_back(queue); }
    Node* prev = curr->prev;
    Node* next = curr->next;
    prev->next = next;
    next->prev = prev;
    return curr;
}

void jobs(int fdout) {
    Node* curr = queue_bg->head;
    while(curr != NULL) {
        char buffer[256];
        sprintf(buffer, "[%d]%c PID:%d\tPGID:%d\tStatus:%d\t%s",curr->jid, back == curr->pid ? '+' : ' ', curr->pid, curr->pid, curr->status, curr->cmd);
        f_write(fdout, buffer, strlen(buffer)+1);
        curr = curr->next;
    }
}

int has_stopped(void) {
    Node* curr = queue_bg->tail;
    while (curr != NULL) {
        if (curr->status == 1) {
            return curr->pid;
        }
        curr = curr->prev; 
    }
    return -1;
}

int get_pid_jid(int jid) {
    Node* curr = queue_bg->tail;
    while (curr != NULL) {
        if (curr->jid == jid) {
            return curr->pid;
        }
        curr = curr->prev;
    }
    return -1;
}

int curr_back(void) {
    Node* curr = queue_bg->tail;
    while (curr != NULL) {
        if (curr->status == 1) {
            return curr->pid;
        }
        curr = curr->prev; 
    }
    return queue_bg->tail == NULL ? -1 : queue_bg->tail->pid;
}

void bg(int kpid) {
    int pid = get_pid_jid(kpid);
    int pid2 = has_stopped();
    if (pid == -1) {
        return;
    } else if (kpid == 0) {
        p_kill(pid2, 2);
    } else {
        p_kill(pid, 2);
    }
}

void fg(int kpid) {
    if (kpid != 0) {
        int pid = get_pid_jid(kpid);
        Node* fg = remove_job(queue_bg, pid);
        if (fg == NULL) { return; }
        pid_global = pid;
        if (fg->status == 1) { p_kill(pid, 2); p_waitpid(pid, NULL, 1); }
        p_waitpid(pid, NULL, 0);
    } else {
        Node* fg = pop_back(queue_bg);
        if (fg == NULL) { return; }
        pid_global = fg->pid;
        if (fg->status == 1) { p_kill(fg->pid, 2); p_waitpid(fg->pid, NULL, 1); }
        p_waitpid(fg->pid, NULL, 0);
    }
}

void poll_background(Queue* queue_bg) {
    Node* curr = queue_bg->head;
    while (curr != NULL) {
        int status = 4;
        pid_t pid_w = p_waitpid(curr->pid, &status, 1);
        if (!pid_w) {
            curr = curr->next;
            continue;
        }
        if (status == 1) {
            curr->status = 1;
        } else if (status == 2) {
            curr->status = 0;
        }
        else if (status == 0 || status == 3) {
            curr->status = -1;
        }
        curr = curr->next;
    }
}

void clean_exited(Queue* queue_bg) {
    Node* curr = queue_bg->head;
    while (curr != NULL) {
        if (curr->status == -1) {
            char buf[256];
            sprintf(buf, "[%d] %c  Done \t %s", curr->jid, back == curr->pid ? '+' : ' ', curr->cmd);
            f_write(STDERR_FILENO, buf, strlen(buf)+1);
            int id = curr->pid;
            curr = curr->next; 
            remove_job(queue_bg, id);
        } else {
            curr = curr->next;
        }
    }
}

int set_status(Queue* queue_bg, pid_t pid, int status, int nice) {
    Node* curr = queue_bg->head;
    while (curr != NULL) {
        if (curr->pid == pid) {
            curr->nice = nice;
            curr->status = status;
            return 0;
        }
        curr = curr->next;
    }
    return -1;
}

void setup_fn(pid_t pid, int is_background, int cmd, char* pcmd, int prio_int, char* name_in) {
    if (is_background) {
        Node* node = create_bg_node(pid);
        node->name = malloc(sizeof(char)*256);
        strcpy(node->name, name_in);
        node->cmd = malloc(sizeof(char)*256);
        strcpy(node->cmd, pcmd);
        push_back(queue_bg, node);
        back = curr_back();
        char buf[256];
        sprintf(buf, "[%d] %d\n", peek_back(queue_bg)->jid, pid);
        f_write(STDERR_FILENO, buf, strlen(buf)+1);
    } else {
        pid_global = pid;
        pcmd_g = pcmd;
        p_waitpid(pid, NULL, 0);
    }
    if (cmd) {
        int p_int = p_nice(pid, prio_int);
        if (p_int != -2) { set_status(queue_bg, pid, 0, p_int); }
    }
}

bool exec_perm(char* path) {
    return get_exec_perm(path);
}

void script_fn(char *argv[], int fdin, int fdout) {
    int file = f_open(abs_path(argv[0]), READ);
    if (!exec_perm(abs_path(argv[0]))) {
        cur_errno = ERR_PERM;
        p_perror("Script has no execute permission.");
        p_exit();
        return;
    }

    char buffer[4096];
    int n  = f_read(file, 4096, buffer);
    char tmp[4096];
    memcpy(tmp, buffer, n + 1);
    // for (int i = 0; i < n; i++) {
    //     fprintf(stderr, "%d: %d\n", i, buffer[i]);
    // }
    int filec = 0;
    if (strtok(tmp, "\n")) { filec++; }
    while (strtok(NULL, "\n")) { filec++; }
    //fprintf(stderr, "filec: %d\n", filec);
    if (filec == 0) { return; }
    char** fargs = malloc(sizeof(char*) * filec+1);
        fargs[0] = strtok(buffer, "\n");
        for (int i = 1; i < filec; i++) { 
            fargs[i] = strtok(NULL, "\n");
        }
    fargs[filec] = "\0";
    int linen = 0;
    while (linen < filec) {
        char* input_line = fargs[linen];
        //fprintf(stderr, "%s\n", input_line);
        // Create temp array
        int len = strlen(input_line);
        char tmp[len + 1];
        memcpy(tmp, input_line, len + 1);

        // Compute number of args assuming no extra whitespace :)
        int argc = 0;
        if (strtok(tmp, " \n\t")) { argc++; }
        while (strtok(NULL, " \n\t")) { argc++; }
        if (argc == 0) {
            poll_background(queue_bg);
            clean_exited(queue_bg);
            continue; 
        }

        // Build array of arguments
        char** args = malloc(sizeof(char*) * argc+1);
        args[0] = strtok(input_line, " \n\t");
        for (int i = 1; i < argc; i++) { 
            args[i] = strtok(NULL, " \n\t");
        }
        args[argc] = "\0";
        int waitStatus;

        // Sets The Method Based on the Input
        int is_background = 0;
        int c = 0;
        int INFD = STDIN_FILENO;
        int OUTFD = (fdout == -1) ? 2 : f_open(abs_path(argv[fdout]), APPEND);
        //fprintf(stderr, "OUTFD: %d\n", OUTFD);
        poll_background(queue_bg);
        clean_exited(queue_bg);
        while (args[argc-1][c] != '\0') { if (args[argc-1][c] == '&') {args[argc-1][c] = ' '; is_background = 1;} c++; }
        if (!strcmp(args[argc-1], "&")) {is_background = 1; args[argc-1] = "\0"; argc--;}

        if (argc-2 >= 0 && !strcmp("<", args[argc-2])) {
            args[argc-2] = "\0";
            int fd = f_open(abs_path(args[argc-1]), READ);
            if (fd == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE"); continue; }
            INFD = fd;
            argc-=2;
            if (argc-2 >= 0 && !strcmp(">", args[argc-2])) {
                args[argc-2] = "\0";
                if (!strcmp(args[argc-1], args[argc+1])) {
                    f_close(fd);
                    f_open(abs_path(args[argc-1]), WRITE);
                    continue;
                }
                int fd2 = f_open(abs_path(args[argc-1]), WRITE);
                if (fd2 == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE MY BROTHER"); continue; }
                OUTFD = fd2;
            } else if (argc-2 >= 0 && !strcmp(">>", args[argc-2])) {
                args[argc-2] = "\0";
                if (!strcmp(args[argc-1], args[argc+1])) { continue; }
                int fd = f_open(abs_path(args[argc-1]), APPEND);
                if (fd == -1) { cur_errno = ERR_NOENT;; p_perror("INVALID FILE MY BROTHER"); continue; }
                OUTFD = fd;
            }
        }

        else if (argc-2 >= 0 && !strcmp(">", args[argc-2])) {
            args[argc-2] = "\0";
            int fd = f_open(abs_path(args[argc-1]), WRITE);
            if (fd == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE MY BROTHER"); continue; }
            OUTFD = fd;
            argc -= 2;
            if (argc-2 >= 0 && !strcmp("<", args[argc-2])) {
                args[argc-2] = "\0";
                if (!strcmp(args[argc-1], args[argc+1])) {continue;}
                int fd2 = f_open(abs_path(args[argc-1]), READ);
                if (fd2 == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE MY BROTHER"); continue; }
                INFD = fd2;
            }
        }

        else if (argc-2 >= 0 && !strcmp(">>", args[argc-2])) {
            args[argc-2] = "\0";
            int fd = f_open(abs_path(args[argc-1]), APPEND);
            if (fd == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE MY BROTHER"); continue; }
            OUTFD = fd;
            argc -= 2;
            if (argc-2 >= 0 && !strcmp("<", args[argc-2])) {
                args[argc-2] = "\0";
                if (!strcmp(args[argc-1], args[argc+1])) { continue; }
                int fd = f_open(abs_path(args[argc-1]), READ);
                if (fd == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE MY BROTHER"); continue; }
                INFD = fd;
            }
        }

        int cmd = 0;
        int prio_int = 0;
        if (!strcmp(args[0], "nice")) {
            cmd = 1;
            if (argc < 2) {f_write(STDERR_FILENO, "Please pass in all arguments\n", 30); continue;}
            prio_int = atoi(args[1]);
            nice_global = prio_int;
            args+=2;
        }

        if (!strcmp(args[0], "nice_pid")) {
            cmd = 1;
            if (argc < 3) {f_write(STDERR_FILENO, "Please pass in all arguments\n", 30); continue;}
            prio_int = atoi(args[1]);
            int pid_l = atoi(args[2]);
            p_nice(pid_l, prio_int);
            continue;
        }

        if (!strcmp(args[0], "bg")) {
            if (argc == 1) {
                bg(0);
            } else {
                bg(atoi(args[1]));
            }
            continue;
        }

        if (!strcmp(args[0], "fg")) {
            if (argc == 1) {
                fg(0);
            } else {
                fg(atoi(args[1]));
            }
            continue;
        }

        if (!strcmp(args[0], "jobs")) {
            jobs(OUTFD);
            continue;
        }

        if (!strcmp(args[0], "man")) {
            char* one = "nice priority command: sets priority of command\n";
            char* two = "nice_pid priority pid: adjusts nice level of process pid\n";
            char* three = "man: lists all possible commands\n";
            char* four = "bg [job_id]: continues the last stopped job, or job_id\n";
            char* five = "fg [job_id]: brings last stopped job (or job_id) to foreground\n";
            char* six = "jobs: list all jobs\n";
            char* seven = "logout: exit the shell\n";
            char* eight = "cat: same cat from bash\n";
            char* nine = "sleep n: sleep for n seconds\n";
            char* ten = "busy: busy wait indefinitely\n";
            char* eleven = "echo: repeats same output\n";
            char* twelve = "ls: lists all files in working directory\n";
            char* thirteen = "touch file: creates empty file, or updates timestamp\n";
            char* fourteen = "mv src dest: renames src to dest\n";
            char* fifteen = "cp src dest: copies src to dest\n";
            char* sixteen = "chmod: changes permissions\n";
            char* seventeen = "ps: lists all processes\n";
            char* eighteen = "kill [-SIGNAL_NAME] pid: sends signal to process name pid\n";

            f_write(OUTFD, one, strlen(one) + 1);
            f_write(OUTFD, two, strlen(two) + 1);
            f_write(OUTFD, three, strlen(three) + 1);
            f_write(OUTFD, four, strlen(four) + 1);
            f_write(OUTFD, five, strlen(five) + 1);
            f_write(OUTFD, six, strlen(six) + 1);
            f_write(OUTFD, seven, strlen(seven) + 1);
            f_write(OUTFD, eight, strlen(eight) + 1);
            f_write(OUTFD, nine, strlen(nine) + 1);
            f_write(OUTFD, ten, strlen(ten) + 1);
            f_write(OUTFD, eleven, strlen(eleven) + 1);
            f_write(OUTFD, twelve, strlen(twelve) + 1);
            f_write(OUTFD, thirteen, strlen(thirteen) + 1);
            f_write(OUTFD, fourteen, strlen(fourteen) + 1);
            f_write(OUTFD, fifteen, strlen(fifteen) + 1);
            f_write(OUTFD, sixteen, strlen(sixteen) + 1);
            f_write(OUTFD, seventeen, strlen(seventeen) + 1);
            f_write(OUTFD, eighteen, strlen(eighteen) + 1);
            continue;
        }

        if (!strcmp(args[0], "logout")) {
            p_logout();
        }

        if (strcmp(args[0], "echo") == 0) {
            pid_t echo = p_spawn(echo_func, args, INFD, OUTFD);
            setup_fn(echo, is_background, cmd, input_line, prio_int, "echo");
        } else if(strcmp(args[0], "sleep") == 0) {
            if (argc < 2) {
                cur_errno = ERR_INVAL;
                p_perror("wrong arguments for sleep");
                continue;
            }
            pid_t sleep = p_spawn(sleep_fn, args, INFD, OUTFD);
            setup_fn(sleep, is_background, cmd, input_line, prio_int, "sleep");
        } else if(strcmp(args[0], "zombify") == 0) {
            pid_t zombie = p_spawn(zombify, args, INFD, OUTFD);
            setup_fn(zombie, is_background, cmd, input_line, prio_int, "zombify");
        } else if(strcmp(args[0], "orphanify") == 0) {
            pid_t orphan = p_spawn(orphanify, args, INFD, OUTFD);
            setup_fn(orphan, is_background, cmd, input_line, prio_int, "orphanify");
        } else if(strcmp(args[0], "hang") == 0) {
            pid_t h = p_spawn(hang, args, INFD, OUTFD);
            p_waitpid(h, &waitStatus, 0);
        } else if(strcmp(args[0], "nohang") == 0) {
            pid_t n = p_spawn(nohang, args, INFD, OUTFD);
            p_waitpid(n, &waitStatus, 0);
        } else if(strcmp(args[0], "recur") == 0) {
            pid_t r = p_spawn(recur, args, INFD, OUTFD);
            p_waitpid(r, &waitStatus, 0);
        } else if(strcmp(args[0], "busy") == 0) {
            pid_t busy = p_spawn(busy_fn, args, INFD, OUTFD);
            setup_fn(busy, is_background, cmd, input_line, prio_int, "busy");
        } else if(strcmp(args[0], "cat") == 0) {
            pid_t cat = p_spawn(cat_fn, args, INFD, OUTFD);
            //fprintf(stderr, "%s\n",args[1]);
            //fprintf(stderr, "b:%d, OUTFD: %d, INFD: %d, strcmp: %d\n", is_background, OUTFD, INFD, !strcmp(args[argc], "\0"));
            if (is_background && (OUTFD == STDERR_FILENO || OUTFD == STDOUT_FILENO) 
            && argc > 1 && (!strcmp(args[1], ">") || !strcmp(args[1], ">>") || !strcmp(args[argc], "\0"))) {
                    p_kill(cat, 1);
                    Node* stopped = create_bg_node(cat);
                    stopped->status = 1;
                    stopped->name = "cat";
                    stopped->cmd = input_line;
                    push_back(queue_bg, stopped);
                    back = curr_back();
                    continue;
                }
            if (is_background && argc == 1) {
                p_kill(cat, 1);
                Node* stopped = create_bg_node(cat);
                stopped->status = 1;
                stopped->name = "cat";
                stopped->cmd = input_line;
                push_back(queue_bg, stopped);
                back = curr_back();
                continue;
             }
            setup_fn(cat, is_background, cmd, input_line, prio_int, "cat");
        } else if(!strcmp(args[0], "ps")) {
            pid_t ps = p_spawn(ps_fn, args, INFD, OUTFD);
            if (is_background && OUTFD == STDERR_FILENO) {p_kill(ps, 1); continue; }
            setup_fn(ps, is_background, cmd, input_line, prio_int, "ps");
        } else if(!strcmp(args[0], "kill")) {
            pid_t kill = p_spawn(kill_fn, args, INFD, OUTFD);
            setup_fn(kill, is_background, cmd, input_line, prio_int, "kill");
        } else if(!strcmp(args[0], "touch")) {
            pid_t touch = p_spawn(touch_fn, args, INFD, OUTFD);
            setup_fn(touch, is_background, cmd, input_line, prio_int, "touch");
        } else if(!strcmp(args[0], "ls")) {
            pid_t ls = p_spawn(ls_fn, args, INFD, OUTFD);
            setup_fn(ls, is_background, cmd, input_line, prio_int, "ls");
        }  else if(!strcmp(args[0], "mv")) {
            pid_t mv = p_spawn(mv_fn, args, INFD, OUTFD);
            setup_fn(mv, is_background, cmd, input_line, prio_int, "mv");
        } else if (!strcmp(args[0], "cp")) {
            pid_t cp = p_spawn(cp_fn, args, INFD, OUTFD);
            setup_fn(cp, is_background, cmd, input_line, prio_int, "cp");
        } else if (!strcmp(args[0], "rm")) {
            pid_t rm = p_spawn(rm_fn, args, INFD, OUTFD);
            setup_fn(rm, is_background, cmd, input_line, prio_int, "rm");
        } else if (!strcmp(args[0], "chmod")) {
            pid_t chmod = p_spawn(chmod_fn, args, INFD, OUTFD);
            setup_fn(chmod, is_background, cmd, input_line, prio_int, "chmod");
        } else if (!strcmp(args[0], "cd")) {
            pid_t cd = p_spawn(cd_fn, args, INFD, OUTFD);
            setup_fn(cd, is_background, cmd, input_line, prio_int, "cd");
        } else if (!strcmp(args[0], "mkdir")) {
            pid_t mkdir = p_spawn(mkdir_fn, args, INFD, OUTFD);
            setup_fn(mkdir, is_background, cmd, input_line, prio_int, "mkdir");
        } else if (!strcmp(args[0], "rmdir")) {
            pid_t rmdir = p_spawn(rmdir_fn, args, INFD, OUTFD);
            setup_fn(rmdir, is_background, cmd, input_line, prio_int, "rmdir");
        } else if (!strcmp(args[0], "pwd")) {
            pid_t pwd2 = p_spawn(pwd_fn, args, INFD, OUTFD);
            setup_fn(pwd2, is_background, cmd, input_line, prio_int, "pwd");
        } else if (!strcmp(args[0], "ln")) {
            pid_t ln = p_spawn(ln_fn, args, INFD, OUTFD);
            setup_fn(ln, is_background, cmd, input_line, prio_int, "ln");
        } else {
            pid_t script = p_spawn(script_fn, args, INFD, OUTFD);
            setup_fn(script, is_background, cmd, input_line, prio_int, args[0]);
        }
        linen++;
    }
    f_close(fdout);
    p_exit();
}

// Shell Function
void shell_func(void) {
    while (1) {
        // Write prompt
        if (f_write(STDOUT_FILENO, PROMPT, strlen(PROMPT)) == -1){
            cur_errno = ERR_PERM;
            p_perror("write");
            exit(EXIT_FAILURE);
        }
        // Read user input
        char cmd_line[MAX_LINE_LENGTH+1];
        int num_chars = f_read(STDIN_FILENO, MAX_LINE_LENGTH, cmd_line);
        if (num_chars == -1){
            cur_errno = ERR_PERM;
            p_perror("read");
            exit(EXIT_FAILURE);
        }
        cmd_line[num_chars] = '\0';

        // Create temp array
        char tmp[MAX_LINE_LENGTH + 1];
        memcpy(tmp, cmd_line, MAX_LINE_LENGTH + 1);

        char* input_line = malloc(sizeof(char) * MAX_LINE_LENGTH+1);
        memcpy(input_line, tmp, MAX_LINE_LENGTH + 1);

        // Compute number of args assuming no extra whitespace :)
        int argc = 0;
        if (strtok(tmp, " \n\t")) { argc++; }
        while (strtok(NULL, " \n\t")) { argc++; }
        if (argc == 0) {
            poll_background(queue_bg);
            clean_exited(queue_bg);
            continue; 
        }

        // Build array of arguments
        char** args = malloc(sizeof(char*) * argc+1);
        args[0] = strtok(cmd_line, " \n\t");
        for (int i = 1; i < argc; i++) { 
            args[i] = strtok(NULL, " \n\t");
        }
        args[argc] = "\0";
        int waitStatus;

        // Sets The Method Based on the Input
        int is_background = 0;
        int c = 0;
        int INFD = STDIN_FILENO;
        int OUTFD = STDERR_FILENO;
        int outarg = -1;
        poll_background(queue_bg);
        clean_exited(queue_bg);
        while (args[argc-1][c] != '\0') { if (args[argc-1][c] == '&') {args[argc-1][c] = ' '; is_background = 1;} c++; }
        if (!strcmp(args[argc-1], "&")) {is_background = 1; args[argc-1] = "\0"; argc--;}

        if (argc-2 >= 0 && !strcmp("<", args[argc-2])) {
            args[argc-2] = "\0";
            int fd = f_open(abs_path(args[argc-1]), READ);
            if (fd == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE"); continue; }
            INFD = fd;
            argc-=2;
            if (argc-2 >= 0 && !strcmp(">", args[argc-2])) {
                args[argc-2] = "\0";
                if (!strcmp(args[argc-1], args[argc+1])) {
                    f_close(fd);
                    f_open(abs_path(args[argc-1]), WRITE);
                    continue;
                }

                int fd2 = f_open(abs_path(args[argc-1]), WRITE);
                if (fd2 == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE"); continue; }
                OUTFD = fd2;
                outarg = argc-1;
            } else if (argc-2 >= 0 && !strcmp(">>", args[argc-2])) {
                args[argc-2] = "\0";
                if (!strcmp(args[argc-1], args[argc+1])) { continue; }
                int fd = f_open(abs_path(args[argc-1]), APPEND);
                if (fd == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE MY BROTHER"); continue; }
                OUTFD = fd;
                outarg = argc-1;
            }
        }

        else if (argc-2 >= 0 && !strcmp(">", args[argc-2])) {
            args[argc-2] = "\0";
            int fd = f_open(abs_path(args[argc-1]), WRITE);
            if (fd == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE MY BROTHER"); continue; }
            OUTFD = fd;
            outarg = argc-1;
            argc -= 2;
            if (argc-2 >= 0 && !strcmp("<", args[argc-2])) {
                args[argc-2] = "\0";
                if (!strcmp(args[argc-1], args[argc+1])) {continue;}
                int fd2 = f_open(abs_path(args[argc-1]), READ);
                if (fd2 == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE MY BROTHER"); continue; }
                INFD = fd2;
            }
        }

        else if (argc-2 >= 0 && !strcmp(">>", args[argc-2])) {
            args[argc-2] = "\0";
            int fd = f_open(abs_path(args[argc-1]), APPEND);
            if (fd == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE MY BROTHER"); continue; }
            OUTFD = fd;
            outarg = argc-1;
            argc -= 2;
            if (argc-2 >= 0 && !strcmp("<", args[argc-2])) {
                args[argc-2] = "\0";
                if (!strcmp(args[argc-1], args[argc+1])) { continue; }
                int fd = f_open(abs_path(args[argc-1]), READ);
                if (fd == -1) { cur_errno = ERR_NOENT; p_perror("INVALID FILE MY BROTHER"); continue; }
                INFD = fd;
            }
        }

        int cmd = 0;
        int prio_int = 0;
        if (!strcmp(args[0], "nice")) {
            cmd = 1;
            if (argc < 2) {f_write(STDERR_FILENO, "Please pass in all arguments\n", 30); continue;}
            prio_int = atoi(args[1]);
            nice_global = prio_int;
            args+=2;
        }

        if (!strcmp(args[0], "nice_pid")) {
            cmd = 1;
            if (argc < 3) {f_write(STDERR_FILENO, "Please pass in all arguments\n", 30); continue;}
            prio_int = atoi(args[1]);
            int pid_l = atoi(args[2]);
            p_nice(pid_l, prio_int);
            continue;
        }

        if (!strcmp(args[0], "bg")) {
            if (argc == 1) {
                bg(0);
            } else {
                bg(atoi(args[1]));
            }
            continue;
        }

        if (!strcmp(args[0], "fg")) {
            if (argc == 1) {
                fg(0);
            } else {
                fg(atoi(args[1]));
            }
            continue;
        }

        if (!strcmp(args[0], "jobs")) {
            jobs(OUTFD);
            continue;
        }

        if (!strcmp(args[0], "man")) {
            char* one = "nice priority command: sets priority of command\n";
            char* two = "nice_pid priority pid: adjusts nice level of process pid\n";
            char* three = "man: lists all possible commands\n";
            char* four = "bg [job_id]: continues the last stopped job, or job_id\n";
            char* five = "fg [job_id]: brings last stopped job (or job_id) to foreground\n";
            char* six = "jobs: list all jobs\n";
            char* seven = "logout: exit the shell\n";
            char* eight = "cat: same cat from bash\n";
            char* nine = "sleep n: sleep for n seconds\n";
            char* ten = "busy: busy wait indefinitely\n";
            char* eleven = "echo: repeats same output\n";
            char* twelve = "ls: lists all files in working directory\n";
            char* thirteen = "touch file: creates empty file, or updates timestamp\n";
            char* fourteen = "mv src dest: renames src to dest\n";
            char* fifteen = "cp src dest: copies src to dest\n";
            char* sixteen = "chmod: changes permissions\n";
            char* seventeen = "ps: lists all processes\n";
            char* eighteen = "kill [-SIGNAL_NAME] pid: sends signal to process name pid\n";

            f_write(OUTFD, one, strlen(one) + 1);
            f_write(OUTFD, two, strlen(two) + 1);
            f_write(OUTFD, three, strlen(three) + 1);
            f_write(OUTFD, four, strlen(four) + 1);
            f_write(OUTFD, five, strlen(five) + 1);
            f_write(OUTFD, six, strlen(six) + 1);
            f_write(OUTFD, seven, strlen(seven) + 1);
            f_write(OUTFD, eight, strlen(eight) + 1);
            f_write(OUTFD, nine, strlen(nine) + 1);
            f_write(OUTFD, ten, strlen(ten) + 1);
            f_write(OUTFD, eleven, strlen(eleven) + 1);
            f_write(OUTFD, twelve, strlen(twelve) + 1);
            f_write(OUTFD, thirteen, strlen(thirteen) + 1);
            f_write(OUTFD, fourteen, strlen(fourteen) + 1);
            f_write(OUTFD, fifteen, strlen(fifteen) + 1);
            f_write(OUTFD, sixteen, strlen(sixteen) + 1);
            f_write(OUTFD, seventeen, strlen(seventeen) + 1);
            f_write(OUTFD, eighteen, strlen(eighteen) + 1);
            continue;
        }

        if (!strcmp(args[0], "logout")) {
            p_logout();
        }

        if (strcmp(args[0], "echo") == 0) {
            pid_t echo = p_spawn(echo_func, args, INFD, OUTFD);
            setup_fn(echo, is_background, cmd, input_line, prio_int, "echo");
        } else if(strcmp(args[0], "sleep") == 0) {
            if (argc < 2) {
                cur_errno = ERR_INVAL;
                p_perror("wrong arguments for sleep");
                continue;
            }
            pid_t sleep = p_spawn(sleep_fn, args, INFD, OUTFD);
            setup_fn(sleep, is_background, cmd, input_line, prio_int, "sleep");
        } else if(strcmp(args[0], "zombify") == 0) {
            pid_t zombie = p_spawn(zombify, args, INFD, OUTFD);
            setup_fn(zombie, is_background, cmd, input_line, prio_int, "zombify");
        } else if(strcmp(args[0], "orphanify") == 0) {
            pid_t orphan = p_spawn(orphanify, args, INFD, OUTFD);
            setup_fn(orphan, is_background, cmd, input_line, prio_int, "orphanify");
        } else if(strcmp(args[0], "hang") == 0) {
            pid_t h = p_spawn(hang, args, INFD, OUTFD);
            p_waitpid(h, &waitStatus, 0);
        } else if(strcmp(args[0], "nohang") == 0) {
            pid_t n = p_spawn(nohang, args, INFD, OUTFD);
            p_waitpid(n, &waitStatus, 0);
        } else if(strcmp(args[0], "recur") == 0) {
            pid_t r = p_spawn(recur, args, INFD, OUTFD);
            p_waitpid(r, &waitStatus, 0);
        } else if(strcmp(args[0], "busy") == 0) {
            pid_t busy = p_spawn(busy_fn, args, INFD, OUTFD);
            setup_fn(busy, is_background, cmd, input_line, prio_int, "busy");
        } else if(strcmp(args[0], "cat") == 0) {
            pid_t cat = p_spawn(cat_fn, args, INFD, OUTFD);
            //fprintf(stderr, "%s\n",args[1]);
            //fprintf(stderr, "b:%d, OUTFD: %d, INFD: %d, strcmp: %d\n", is_background, OUTFD, INFD, !strcmp(args[argc], "\0"));
            if (is_background && (OUTFD == STDERR_FILENO || OUTFD == STDOUT_FILENO) 
            && argc > 1 && (!strcmp(args[1], ">") || !strcmp(args[1], ">>") || !strcmp(args[argc], "\0"))) {
                    p_kill(cat, 1);
                    Node* stopped = create_bg_node(cat);
                    stopped->status = 1;
                    stopped->name = "cat";
                    stopped->cmd = input_line;
                    push_back(queue_bg, stopped);
                    back = curr_back();
                    continue;
                }
            if (is_background && argc == 1) {
                p_kill(cat, 1);
                Node* stopped = create_bg_node(cat);
                stopped->status = 1;
                stopped->name = "cat";
                stopped->cmd = input_line;
                push_back(queue_bg, stopped);
                back = curr_back();
                continue;
             }
            setup_fn(cat, is_background, cmd, input_line, prio_int, "cat");
        } else if(!strcmp(args[0], "ps")) {
            pid_t ps = p_spawn(ps_fn, args, INFD, OUTFD);
            if (is_background && OUTFD == STDERR_FILENO) {p_kill(ps, 1); continue; }
            setup_fn(ps, is_background, cmd, input_line, prio_int, "ps");
        } else if(!strcmp(args[0], "kill")) {
            pid_t kill = p_spawn(kill_fn, args, INFD, OUTFD);
            setup_fn(kill, is_background, cmd, input_line, prio_int, "kill");
        } else if(!strcmp(args[0], "touch")) {
            pid_t touch = p_spawn(touch_fn, args, INFD, OUTFD);
            setup_fn(touch, is_background, cmd, input_line, prio_int, "touch");
        } else if(!strcmp(args[0], "ls")) {
            pid_t ls = p_spawn(ls_fn, args, INFD, OUTFD);
            if (is_background && OUTFD == STDERR_FILENO) {p_kill(ls, 1); continue; }
            setup_fn(ls, is_background, cmd, input_line, prio_int, "ls");
        }  else if(!strcmp(args[0], "mv")) {
            pid_t mv = p_spawn(mv_fn, args, INFD, OUTFD);
            setup_fn(mv, is_background, cmd, input_line, prio_int, "mv");
        } else if (!strcmp(args[0], "cp")) {
            pid_t cp = p_spawn(cp_fn, args, INFD, OUTFD);
            setup_fn(cp, is_background, cmd, input_line, prio_int, "cp");
        } else if (!strcmp(args[0], "rm")) {
            pid_t rm = p_spawn(rm_fn, args, INFD, OUTFD);
            setup_fn(rm, is_background, cmd, input_line, prio_int, "rm");
        } else if (!strcmp(args[0], "chmod")) {
            pid_t chmod = p_spawn(chmod_fn, args, INFD, OUTFD);
            setup_fn(chmod, is_background, cmd, input_line, prio_int, "chmod");
        } else if (!strcmp(args[0], "cd")) {
            pid_t cd = p_spawn(cd_fn, args, INFD, OUTFD);
            setup_fn(cd, is_background, cmd, input_line, prio_int, "cd");
        } else if (!strcmp(args[0], "mkdir")) {
            pid_t mkdir = p_spawn(mkdir_fn, args, INFD, OUTFD);
            setup_fn(mkdir, is_background, cmd, input_line, prio_int, "mkdir");
        } else if (!strcmp(args[0], "rmdir")) {
            pid_t rmdir = p_spawn(rmdir_fn, args, INFD, OUTFD);
            setup_fn(rmdir, is_background, cmd, input_line, prio_int, "rmdir");
        } else if (!strcmp(args[0], "pwd")) {
            pid_t pwd2 = p_spawn(pwd_fn, args, INFD, OUTFD);
            setup_fn(pwd2, is_background, cmd, input_line, prio_int, "pwd");
        } else if (!strcmp(args[0], "ln")) {
            pid_t ln = p_spawn(ln_fn, args, INFD, OUTFD);
            setup_fn(ln, is_background, cmd, input_line, prio_int, "ln");
        } else {
            pid_t script = p_spawn(script_fn, args, INFD, outarg);
            setup_fn(script, is_background, cmd, input_line, prio_int, args[0]);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {cur_errno = ERR_INVAL; p_perror("please specify a fs"); return 0;}
    if (mount_fs(argv[1]) == -1) {cur_errno = ERR_INVAL; p_perror("please specify a fs"); return 0;};
    signal(SIGINT, signal_handler);
    signal(SIGTSTP, signal_handler);
    //name = malloc(sizeof(char)*256);
    queue_bg = create_queue();
    if (argc < 3) {
        p_setup_scheduler(shell_func, "log.txt");
    } else {
        p_setup_scheduler(shell_func, argv[2]);
    }
}