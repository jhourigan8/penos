#ifndef ERRORS
#define ERRORS

#define ERR_PERM 1
#define ERR_NOENT 2
#define ERR_ACCES 3
#define ERR_NOTDIR 4
#define ERR_DIR 5
#define ERR_INVAL 6

extern int cur_errno;

void p_perror(char* string);

#endif