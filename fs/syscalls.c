#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "table.h"
#include "syscalls.h"
#include "../error.h"

// initiate file table
Table fd_table;
const char* MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/**
 * @brief The current working directory.
 */
char* pwd = "\0";

/*
    Initiate file descriptor table in the kernel
*/
void init_table() {
    init(&fd_table);
}

/*
    Opens file
    Returns file descriptor, -1 otherwise
*/
int f_open(const char *fname, int mode) {
    TNode *node = add(&fd_table, (char*)fname, mode);

    if (!node) {
        return -1;
    }

    return node->file_descriptor;
}

/*
 * Helper to read contents of files specified by names into a vec.
 * Returns -1 in size field of vec if one of the files doesn't exist.
*/
Vec read_files(char** names, int num) {
    int size = 0;
    for (int i = 0; i < num; ++i) {
        File f = get_file(names[i], true);
        if (f.name[0] == 0) { Vec vec = { NULL, -1 }; return vec; }
        size += f.size;
    }
    uint8_t* buf = (uint8_t*) malloc(size);
    size = 0;
    for (int i = 0; i < num; ++i) {
        File f = get_file(names[i], true);
        read_file(names[i], 0, buf + size, f.size);
        size += f.size;
    }
    Vec vec = { buf, size };
    return vec;
}

/*
    Reads file
    Returns number of bytes on success, -1 otherwise
*/
int f_read(int fd, int n, char *buf) {
    if (fd < 3) {
        int c = read(fd, buf, n);
        return c;
    }

    TNode *node = get_fd(&fd_table, fd);

    //error
    if (!node || node->mode != READ) {
        return -1;
    }

    File f = get_file(node->file_name, true);
    if (!(f.perm & READ_PERM)) {
        cur_errno = ERR_ACCES;
        p_perror("no read permission");
        return -1;
    }
    int r = f.size - node->file_pointer;
    if (r > n) { r = n; }
    else if (node->file_pointer > f.size) { return 0; }
    //write to buffer and change pointer
    int c = read_file((char*)node->file_name, node->file_pointer, (uint8_t*) buf, r);
    if (c >= 0) {
        node->file_pointer += c;
    }
    
    return c;    
}

/*
    Writes to file
    Returns number of bytes on success, -1 otherwise
*/
int f_write(int fd, const char *str, int n) {
    if (fd < 3) {
        int c = write(fd, str, n);
        return c;
    }

    TNode *node = get_fd(&fd_table, fd);

    //error
    if (!node || !(node->mode == WRITE || node->mode == APPEND)) {
        return -1;
    }

    File file = get_file(node->file_name, true);
    if (!(file.perm & WRITE_PERM)) {
        cur_errno = ERR_ACCES;
        p_perror("no write permission");
        return -1;
    }
    //write

    write_file((char*)node->file_name, node->file_pointer, (uint8_t*) str, n, true);
    //increment by number of bytes written
    node->file_pointer = seek_data(node->file_pointer, n);
    
    return n;
}

/*
    Closes file
    Returns 0 on success, -1 otherise
*/
int f_close(int fd) {
    TNode *node = delete(&fd_table, fd);
    if (!node) {
        return -1;
    }

    free(node);

    return 0;
}

/*
    Closes file
*/
void f_unlink(const char *fname) {
    remove_file((char*)fname);
}

/*
    lseeks file to offset
*/
void f_lseek(int fd, int offset, int whence) {
    TNode *node = get_fd(&fd_table, fd);
    File f = get_file((char*)node->file_name, true);

    if (whence == SEEK_SET) {
        node->file_descriptor = seek_data(f.first_block * 64, offset);
    } else if (whence == SEEK_CUR) {
        node->file_descriptor = seek_data(node->file_descriptor, offset);
    } else if (whence == SEEK_END) {
        node->file_descriptor = seek_data(f.first_block * 64, f.size);
        node->file_descriptor = seek_data(node->file_descriptor, offset);
    }
}

/*
    Returns file or list of files in directory if filename is null
*/
char** f_ls(const char *filename) {
    char** out = NULL;

    if (!filename) {
        char* path = abs_path("");
        File* list = list_directory(path);
        int count = 0;
        for (int i = 0; list[i].name[0] != 0; i++) {
            count++;
        }
        out = malloc(sizeof(char*) * (count + 1));
        out[count] = "\0";
        int fb_len = 0;
        int size_len = 0;
        int day_len = 0;
        int name_len = 0;
        char str[32];
        for (int i = 0; list[i].name[0] != 0; i++) {
            sprintf(str, "%hu", list[i].first_block);
            if (strlen(str) > fb_len) { fb_len = strlen(str); }
            memset(str, 0, strlen(str));
            sprintf(str, "%u", list[i].size);
            if (strlen(str) > size_len) { size_len = strlen(str); }
            memset(str, 0, strlen(str));
            struct tm *time_data = localtime(&list[i].mtime);
            sprintf(str, "%u", time_data->tm_mday);
            if (strlen(str) > day_len) { day_len = strlen(str); }
            memset(str, 0, strlen(str));
            if (strlen(list[i].name) > name_len) { name_len = strlen(list[i].name); }
            memset(str, 0, strlen(str));
        }
        for (int i = 0; list[i].name[0] != 0; i++) {
            out[i] = malloc(sizeof(char) * 32);
            struct tm *time_data = localtime(&list[i].mtime);
            sprintf(out[i], "%*hu %c%c%c %*u %s %*u %02u:%02u %.*s\n", 
                fb_len, list[i].first_block, 
                list[i].perm & EXECUTE_PERM ? 'x' : '-', 
                list[i].perm & READ_PERM ? 'r' : '-',
                list[i].perm & WRITE_PERM ? 'w' : '-',
                size_len, list[i].size, 
                MONTHS[time_data->tm_mon],
                day_len, time_data->tm_mday,
                time_data->tm_hour,
                time_data->tm_min,
                name_len, list[i].name
            );
        }
    } else {
        char* filename2 = malloc(sizeof(char) * (strlen(filename) + 1));
        filename2[strlen(filename)] = '\0';
        strcpy(filename2, filename);
        char* path = abs_path(filename2);
        File* list = list_directory(path);
        bool exists = false;
        for (int i = 0; list[i].name[0] != 0; i++) {
            if (!strcmp(list[i].name, filename)) {
                exists = true;
            }
        }
        if(!exists) {
            return out;
        }
        out = malloc(sizeof(char*) * 2);
        out[1] = "\0";
        int fb_len = 0;
        int size_len = 0;
        int day_len = 0;
        int name_len = 0;
        char str[32];
        out[0] = malloc(sizeof(char) * 32);
        for (int i = 0; list[i].name[0] != 0; i++) {
            if (!strcmp(list[i].name, filename)) {
                sprintf(str, "%hu", list[i].first_block);
                if (strlen(str) > fb_len) { fb_len = strlen(str); }
                memset(str, 0, strlen(str));
                sprintf(str, "%u", list[i].size);
                if (strlen(str) > size_len) { size_len = strlen(str); }
                memset(str, 0, strlen(str));
                struct tm *time_data = localtime(&list[i].mtime);
                sprintf(str, "%u", time_data->tm_mday);
                if (strlen(str) > day_len) { day_len = strlen(str); }
                memset(str, 0, strlen(str));
                if (strlen(list[i].name) > name_len) { name_len = strlen(list[i].name); }
                memset(str, 0, strlen(str));
            }
        }
        for (int i = 0; list[i].name[0] != 0; i++) {
            if (!strcmp(list[i].name, filename)) {
                struct tm *time_data = localtime(&list[i].mtime);
                sprintf(out[0], "%*hu %c%c%c %*u %s %*u %02u:%02u %.*s\n", 
                    fb_len, list[i].first_block, 
                    list[i].perm & EXECUTE_PERM ? 'x' : '-', 
                    list[i].perm & READ_PERM ? 'r' : '-',
                    list[i].perm & WRITE_PERM ? 'w' : '-',
                    size_len, list[i].size, 
                    MONTHS[time_data->tm_mon],
                    day_len, time_data->tm_mday,
                    time_data->tm_hour,
                    time_data->tm_min,
                    name_len, list[i].name
                );
            }
        }
    }

    return out;
}

void arg_error(char* err) {
    if (write(STDOUT_FILENO, err, strlen(err)) == -1){
        p_perror(err);
        exit(EXIT_FAILURE);
    }
}

void f_touch(char* args[]) {
    if (!strcmp(args[1], "\0")) { arg_error("touch: missing file operand\n"); return; }
    int i = 1;
    while(strcmp(args[i], "\0")) {
        char* path = abs_path(args[i]);
        create_file(path, REGULAR_FILE);
        write_file(path, 0, NULL, 0, true);
        i++;
    }
}

void f_mv(char* args[]) {
    int argc = 0;
    while (strcmp(args[argc], "\0")) { argc++; }
    if (argc == 1) { arg_error("mv: Missing source file\n"); return; }
    if (argc == 2) { arg_error("mv: Missing destination file\n"); return; }
    if (argc > 3) { arg_error("mv: Too many arguments\n"); return; }
    char* p1 = abs_path(args[1]);
    char* p2 = abs_path(args[2]);
    File f = get_file(p1, false);
    if (f.name[0] == 0) { arg_error("mv: cannot find source file\n"); return; }
    File tgt = get_file(p2, false);
    int i = 2;
    if (tgt.type == DIRECTORY_FILE) { 
        i = 1;
    }
    char* name = strtok(args[i], "/");
    char* tmp = strtok(NULL, "/");
    while (tmp != NULL) {
        name = tmp;
        tmp = strtok(NULL, "/");
    }
    strcpy(f.name, name);
    // Thing to replace is p2path/p1name instead of p2path
    if (tgt.type == DIRECTORY_FILE) {
        char* p2_new = (char*) malloc(strlen(p2) + strlen(name) + 2); // slash!
        strcpy(p2_new, p2);
        strcat(p2_new, "/");
        strcat(p2_new, name);
        p2 = p2_new;
    }
    if (create_file(p2, f.type) == -1) {
        File dest = get_file(p2, false);
        if (dest.type != DIRECTORY_FILE && f.type == DIRECTORY_FILE) { 
            cur_errno = ERR_DIR;
            p_perror("mv error"); 
            return;
        }
        if (truncate_file(p2, false) == -1) {
            cur_errno = ERR_DIR;
            p_perror("mv error");  
            return;
        }
    }
    set_file(p2, f, false);
    cleanup_file(remove_file(p1));
}

void f_cp(char* args[]) {
    int argc = 0;
    while (strcmp(args[argc], "\0")) { argc++; }
    if (argc == 1) { arg_error("cp: missing source file\n"); return; }
    bool h_src = strcmp(args[1], "-h") == 0;
    if (argc == 2) { 
        if (!h_src) { arg_error("cp: missing destination file\n"); return; }
        if (h_src) { arg_error("cp: missing source file\n"); return; }
    }
    bool h_dest = strcmp(args[2], "-h") == 0;
    if (argc > 4) { arg_error("cp: too many arguments\n"); return; }
    Vec v;
    if (h_src) {
        int src_fd = open(args[2], O_RDONLY);
        if (src_fd == -1) { arg_error("cp: cannot open source file\n"); return; }
        struct stat st;
        if (fstat(src_fd, &st) == -1) { arg_error("cp: cannot stat source file\n"); return; };
        v.size = st.st_size;
        v.buf = (uint8_t*) malloc(v.size);
        read(src_fd, v.buf, v.size);
        close(src_fd);
    } else {
        v = read_files(&args[1], 1);
        if (v.size == -1) { arg_error("cp: cannot find source file\n"); return; };
    }
    if (h_dest) { 
        int dest_fd = open(args[3], O_WRONLY | O_CREAT | O_TRUNC, 0777); 
        if (dest_fd == -1) { arg_error("cp: cannot open destination file\n"); return; }
        write(dest_fd, v.buf, v.size);
        close(dest_fd);
    } else {
        char* path = abs_path(args[h_src ? 3 : 2]);
        File dest = get_file(path, true);
        if (dest.type == DIRECTORY_FILE) {
            char* name = strtok(args[h_src ? 2 : 1], "/");
            char* tmp = strtok(NULL, "/");
            while (tmp != NULL) {
                name = tmp;
                tmp = strtok(NULL, "/");
            }
            char* dest_new = (char*) malloc(strlen(path) + strlen(name) + 2); // slash!
            strcpy(dest_new, path);
            strcat(dest_new, "/");
            strcat(dest_new, name);
            path = dest_new;
        }
        if (create_file(path, REGULAR_FILE) == -1) {
            if (truncate_file(path, true) == -1) {
                cur_errno = ERR_PERM;
                p_perror("cp");
                return;
            }
        }
        if (write_file(path, 0, v.buf, v.size, true) == -1) {
            cur_errno = ERR_PERM;
            p_perror("cp");
            return;
        }
    }
}

void f_rm(char* args[]) {
    int argc = 0;
    while (strcmp(args[argc], "\0")) { argc++; }
    if (argc == 1) { arg_error("rm: missing source file\n"); return; }
    for (int i = 1; i < argc; i++) {
        char* path = abs_path(args[i]);
        if (truncate_file(path, false) == -1) { arg_error("rm: cannot find file\n"); break; }
        cleanup_file(remove_file(path));
    }
}

void f_chmod(char* args[]) {
    File f;
    int perm;
    if (args[1][1] == 'x') { 
        perm = EXECUTE_PERM;
    } else if (args[1][1] == 'r') {
        perm = READ_PERM;
    } else if (args[1][1] == 'w') {
        perm = WRITE_PERM;
    } else {
        arg_error("chmod: invalid permssions modifier\n");
        return;
    }
    char* path = abs_path(args[2]);
    f = get_file(path, true);
    if (f.name[0] == 0) { arg_error("chmod: cannot find file\n"); return; }
    if (args[1][0] == '-') { 
        f.perm &= (0x7 - perm);
    } else if (args[1][0] == '+') {
        f.perm |= perm;
    } else {
        arg_error("chmod: invalid permssions modifier\n");
        return;
    }
    if (f.name[0] == 0) { arg_error("chmod: cannot find file\n"); return; };
    set_file(path, f, true);
}

/**
 * @brief Computes a parsed absolute path string to the file located at `name`.
 *
 * If `name` is an absolute path (begins with '\') it is returned.
 * If `name` is a relative path we append it to the global `pwd` variable.
 * "Parsed" means that all dots are removed from the returned string.
 * Single dots '.' do nothing.
 * Double dots '..' go up one directory (unless at root).
 *
 * @return The path from the root directory to the file.
 * @param name The location of the file.
 */
char* abs_path(char* name) {
    // Get full path with extra chars (. and ..)
    char* path_str;
    int len;
    if (name[0] != '/') {
        len = strlen(pwd) + strlen(name) + 1; // middle slash
        path_str = (char*) malloc(len + 1);
        strcpy(path_str, pwd);
        strcat(path_str, "/");
        strcat(path_str, name);
    } else {
        len = strlen(name) + 1;
        path_str = (char*) malloc(len + 1);
        strcat(path_str, name);
    }
    // Compute number of tokens
    char tmp[len + 1];
    strcpy(tmp, path_str);
    int argc = 0;
    if (strtok(tmp, "/")) { argc++; }
    while (strtok(NULL, "/")) { argc++; }
    // Build raw token sequence
    char** tokens = malloc(sizeof(char*) * argc);
    tokens[0] = strtok(path_str, "/");
    for (int i = 1; i < argc; i++) { 
        tokens[i] = strtok(NULL, "/");
    }
    // Build logical token sequence
    // Recall: . = do nothing, .. = go back a token
    char** path = malloc(sizeof(char*) * argc);
    int i = 0; // ptr past last token
    for (int j = 0; j < argc; ++j) {
        if ((strcmp(tokens[j], "..")) == 0) {
            if (i > 0) { --i; }
        } else if ((strcmp(tokens[j], ".")) == 0) {
            // do nothing
        } else {
            path[i] = tokens[j];
            ++i;
        }
    }
    // Build string
    len = 2;
    for (int j = 0; j < i; ++j) {
        len += strlen(path[j]) + 1; // slash!
    }
    char* str = (char*) malloc(len);
    str[0] = '\0';
    for (int j = 0; j < i; ++j) {
        strcat(str, "/");
        strcat(str, path[j]);
    }
    return str;
}

char* f_cat(char* args[]) {
    int argc = 0;
    while (strcmp(args[argc], "\0")) { argc++; }
    char* out = malloc(sizeof(char) * 4096);
    char* flag = args[argc - 2];
    bool append_f = strcmp(flag, "-a") == 0;
    bool write_f = strcmp(flag, "-w") == 0;
    Vec vec;
    if (argc == 3 && (append_f || write_f)) {
        vec.buf = (uint8_t*) malloc(4096);
        vec.size = read(STDIN_FILENO, vec.buf, 4096);
    } else {
        vec = read_files(&args[1], (append_f || write_f) ? argc - 3 : argc - 1);
        if (vec.size == -1) { cur_errno = ERR_PERM; p_perror("cat"); return NULL; };
    }
    if (append_f) {
        char* path = abs_path(args[argc - 1]);
        if (create_file(path, REGULAR_FILE) == -1) {
            File f = get_file(path, true);
            if (f.name[0] != 0 && f.type == DIRECTORY_FILE) {
                cur_errno = ERR_DIR;
                p_perror("cat");
                return NULL;
            }
        }
        if (write_file(path, get_file(path, true).size, vec.buf, vec.size, true)) {
            cur_errno = ERR_PERM;
            p_perror("cat");
            return NULL;
        }
    } else if (write_f) {
        char* path = abs_path(args[argc - 1]);
        if (create_file(path, REGULAR_FILE) == -1)  {
            if (truncate_file(path, true) == -1) {
                cur_errno = ERR_PERM;
                p_perror("cat");
                return NULL;
            }
        }
        if (write_file(path, 0, vec.buf, vec.size, true) == -1) {
            cur_errno = ERR_PERM;
            p_perror("cat"); return NULL;
        }
    } else {
        sprintf(out, "%s", vec.buf);
        return out;
        //write(STDOUT_FILENO, vec.buf, vec.size);
    }
    return out;
}

void f_cd(char* args[]) {
    int argc = 0;
    while (strcmp(args[argc], "\0")) { argc++; }
    if (argc == 1) { arg_error("cd: Missing operand\n"); return; }
    if (argc > 2) { arg_error("cd: Too many arguments\n"); return; }
    char* path = abs_path(args[1]);
    File f = get_file(path, true);
    if (f.name[0] == 0) { cur_errno = ERR_NOENT; p_perror("cd error"); return; }
    if (f.type != DIRECTORY_FILE) { cur_errno = ERR_NOTDIR; p_perror("cd error"); return; }
    pwd = path;
}

void f_mkdir(char* args[]) {
    int argc = 0;
    while (strcmp(args[argc], "\0")) { argc++; }
    for (int i = 1; i < argc; i++) {
        char* path = abs_path(args[i]);
        if (create_file(path, DIRECTORY_FILE) == -1) { 
            cur_errno = ERR_PERM;
            p_perror("mkdir"); 
            return; 
        }
    }
}

void f_rmdir(char* args[]) {
    int argc = 0;
    while (strcmp(args[argc], "\0")) { argc++; }
    if (argc == 1) { arg_error("rmdir: Missing operand\n"); return; }
    for (int i = 1; i < argc; i++) {
        char* path = abs_path(args[i]);
        File f = get_file(path, false);
        if (f.type != DIRECTORY_FILE) { cur_errno = ERR_NOTDIR; p_perror("rmdir error"); return; }
        if (truncate_file(path, false) == -1) { 
            cur_errno = ERR_PERM;
            p_perror("rmdir"); return; 
        }
        cleanup_file(remove_file(path));
    }
}

char* f_pwd(char* args[]) {
    int argc = 0;
    while (strcmp(args[argc], "\0")) { argc++; }
    if (argc > 1) { arg_error("pwd: Too many arguments\n"); return "\n"; }
    if (strlen(pwd) > 0) {
        return strcat(pwd, "\n");
    } else {
        return "/\n";
    } 

}

void f_ln(char* args[]) {
    int argc = 0;
    while (strcmp(args[argc], "\0")) { argc++; }
    if (argc <= 2) { arg_error("ln: Missing target file\n"); return; }
    if (strcmp(args[1], "-s") != 0) { arg_error("ln: Hard links not supported\n"); return; }
    if (argc == 3) { arg_error("ln: Missing link name\n"); return; }
    if (argc > 4) { arg_error("ln: Too many arguments\n"); return; }
    char* path = abs_path(args[3]);
    File file = get_file(path, false);
    if (file.name[0] != 0) {
        cur_errno = ERR_PERM;
        p_perror("ln error"); return;
    }
    if (create_file(path, LINK_FILE) == -1) { 
        cur_errno = ERR_PERM;
        p_perror("ln error"); return;
    }
    char* target = abs_path(args[2]);
    if (write_file(path, 0, (uint8_t*) target, strlen(target) + 1, false) == -1) {
        cur_errno = ERR_PERM;
        p_perror("ln error");
        return;
    }
}

bool get_exec_perm(char* path) {
    File file = get_file(path, true);
    return file.perm & 1;
}