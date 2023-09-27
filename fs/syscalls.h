/*
Header for System Calls
*/
#ifndef SYSCALLS
#define SYSCALLS
#include "table.h"
#include "filesys.h"

extern const char* MONTHS[];

/*
 * A sized byte array type.
 */
typedef struct vector {
    uint8_t* buf;
    int size;
} Vec;

void arg_error(char* err);

/*
    Initiate file descriptor table in the kernel
*/
void init_table();

/*
    Opens file
    Returns file descriptor, -1 otherwise
*/
int f_open(const char *fname, int mode);

/*
    Reads file
    Returns number of bytes on success, -1 otherwise
*/
int f_read(int fd, int n, char *buf);

/*
    Writes to file
    Returns number of bytes on success, -1 otherwise
*/
int f_write(int fd, const char *str, int n);

/*
    Closes file
    Returns 0 on success, -1 otherise
*/
int f_close(int fd);

/*
    Closes file
*/
void f_unlink(const char *fname);

/*
    lseeks file to offset
*/
void f_lseek(int fd, int offset, int whence);

/*
    Returns file or list of files in directory if filename is null
*/
char** f_ls(const char *filename);

void f_touch(char* argv[]);

void f_mv(char* argv[]);

void f_cp(char* argv[]);

void f_rm(char* argv[]);

void f_chmod(char* argv[]);

void f_cd(char* argv[]);

void f_mkdir(char* argv[]);

void f_rmdir(char* argv[]);

char* f_pwd(char* argv[]);

void f_ln(char* argv[]);

char* f_cat(char* argv[]);

Vec read_files(char** names, int num);

char* abs_path(char* name);

bool get_exec_perm(char* path);

#endif