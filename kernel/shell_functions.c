#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "queue.h"
#include "scheduler.h"

void sleep_fn(char *argv[]) {
    int n = atoi(argv[1]);
    p_sleep(10*n);
    p_exit();
}

void busy_fn() {
    while(1);
    p_exit();
}

void orphan_child() {
    while(1);
    p_exit();
}

void zombie_child() {
    p_exit();
}

void zombify() {
    char **args = malloc(sizeof(char*));
    args[0] = "zombie_child";
    p_spawn(zombie_child, args, STDIN_FILENO, STDOUT_FILENO);
    while(1);
    p_exit();
}

void orphanify() {
    char **args = malloc(sizeof(char*));
    args[0] = "orphan_child";
    p_spawn(orphan_child, args, STDIN_FILENO, STDOUT_FILENO);
    p_exit();
}



