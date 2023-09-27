#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "fs/syscalls.h"

int cur_errno;

void p_perror(char* string) {
    char* part;
    switch(cur_errno) {
        case ERR_PERM:
            part = "OPERATION NOT PERMITTED: ";
            break;
        case ERR_NOENT:
            part = "NO SUCH FILE/DIRECTORY: ";
            break;
        case ERR_ACCES:
            part = "PERMISSION DENIED: ";
            break;
        case ERR_NOTDIR:
            part = "NOT A DIRECTORY: ";
            break;
        case ERR_DIR:
            part = "IS A DIRECTORY: ";
            break;
        case ERR_INVAL:
            part = "INVALID ARGUMENT: ";
            break;
    }
    char out[256] = "";
    strcat(out, part);
    strcat(out, string);
    strcat(out, "\n");
    f_write(STDERR_FILENO, out, strlen(out) + 1);
}