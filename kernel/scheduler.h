#ifndef SCHEDULER
#define SCHEDULER
#include <unistd.h>
#include <sys/types.h>

int p_nice(pid_t pid, int priority);
void p_sleep(unsigned int ticks);
pid_t p_spawn(void (*func)(), char *argv[], int fd0, int fd1);
int p_kill(pid_t pid, int sig);
pid_t p_waitpid(pid_t pid, int *wstatus, int nohang);
void init_scheduler(char* logname);
void test_setup(void);
char** p_ps(void);
void p_exit(void);
void p_setup_scheduler(void (*func)(), char* logname);
void p_logout(void);

void hang(void);
void nohang(void);
void recur(void);
#endif