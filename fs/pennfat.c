#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>
#include "pennfat.h"
#include "filesys.h"
#include "../error.h"

/**
 * @file pennfat.c
 * @brief An implementation of filesystem related shell commands.
 */
 char* pwd2;

/**
 * @brief Maximum supported user input line length.
 */
#define MAX_LINE_LENGTH 8192

/**
 * @brief An array of months for `ls` pretty-print.
 */
const char* MONTHS2[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/**
 * @brief Is there a mounted filesystem or not.
 */
bool mounted = false;

/**
 * @brief A sized byte array type.
 */
typedef struct vector {
    /**
    * @brief A buffer containing bytes
    */
    uint8_t* buf;
    /**
    * @brief Size of buffer.
    */
    int size;
} Vec;

/**
 * @brief Simple helper to print argument errors with less boilerplate.
 */
void arg_error2(char* err) {
    if (write(STDOUT_FILENO, err, strlen(err)) == -1) {
        p_perror(err);
        exit(EXIT_FAILURE);
    }
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
char* abs_path2(char* name) {
    // Get full path with extra chars (. and ..)
    char* path_str;
    int len;
    if (name[0] != '/') {
        len = strlen(pwd2) + strlen(name) + 1; // middle slash
        path_str = (char*) malloc(len + 1);
        strcpy(path_str, pwd2);
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

/**
 * @brief Reads the contents of `num` files `names` into a buffer.
 *
 * Skips any link files to read the file which is pointed to.
 * If any file doesn't exist or can't be read the function fails.
 *
 * @return A vector containing the files' contents on success
 *         and an empty vector with size -1 on failure.
 * @param names A sequence of absolute paths to read from.
 * @param num The number of files in the input.
 */
Vec read_files2(char** names, int num) {
    int size = 0;
    for (int i = 0; i < num; ++i) {
        char* path = abs_path2(names[i]);
        File f = get_file(path, true);
        if (f.name[0] == 0) { Vec vec = { NULL, -1 }; return vec; }
        size += f.size;
    }
    uint8_t* buf = (uint8_t*) malloc(size);
    size = 0;
    for (int i = 0; i < num; ++i) {
        char* path = abs_path2(names[i]);
        File f = get_file(path, true);
        if (read_file(path, 0, buf + size, f.size) == -1) { Vec vec = { NULL, -1 }; return vec; }
        size += f.size;
    }
    Vec vec = { buf, size };
    return vec;
}

/**
 * @brief Makes a filesystem in the current directory on the host machine.
 *
 * Prints an error on malformed input.
 *
 * @param args[1] Name of the filesystem.
 * @param args[2] Number of blocks in the FAT, from 1-32.
 * @param args[3] Block size config, from 0-4.
 */
void pf_mkfs(int argc, char** args) {
    if (argc == 1) { arg_error2("mkfs: Missing filesystem name\n"); return; }
    if (argc == 2) { arg_error2("mkfs: Missing blocks in fat\n"); return; }
    if (argc == 3) { arg_error2("mkfs: Missing blocks size config\n"); return; }
    if (argc > 4) { arg_error2("mkfs: Too many arguments\n"); return; }
    int new_fat_blocks = atoi(args[2]);
    if (new_fat_blocks < 1 || new_fat_blocks > 32) { 
        arg_error2("mkfs: Blocks in fat must be integer in [1..32]\n"); return; 
    }
    int new_block_size_config = atoi(args[3]);
    if (new_block_size_config < 0 || new_block_size_config > 4) {
        arg_error2("mkfs: Block size config must be integer in [0..4]\n"); return; 
    }
    if (init_fs(args[1], new_fat_blocks, new_block_size_config) == -1) {
        cur_errno = ERR_PERM;
        p_perror("mkfs");
        return;
    }
}

/**
 * @brief Mount a filesystem.
 *
 * Sets `pwd` and `mounted` variables appropriately.
 * Prints an error if another filesystem is already mounted.
 *
 * @param args[1] Name of the filesystem.
 */
void pf_mount(int argc, char** args) {
    if (argc == 1) { cur_errno = ERR_INVAL; arg_error2("mount: Missing filesystem name\n"); return; }
    if (argc > 2) { cur_errno = ERR_INVAL; arg_error2("mount: Too many arguments\n"); return; }
    if (mounted) { cur_errno = ERR_INVAL; arg_error2("mount: Another filesystem is currently mounted\n"); return; }
    if (mount_fs(args[1]) == -1) { 
        cur_errno = ERR_PERM;
        p_perror("mount"); 
        return; 
    }
    mounted = true;
    pwd2 = (char*) malloc(1);
    pwd2[0] = '\0';
}

/**
 * @brief Unmount currently mounted filesystem.
 *
 * Resets `mounted` to false.
 * Prints an error if no filesystem is currently mounted.
 */
void pf_umount(int argc, char** args) {
    if (argc > 1) { arg_error2("umount: Too many arguments\n"); return; }
    if (!mounted) { arg_error2("umount: No filesystem mounted\n"); return; } 
    if (unmount_fs() == -1) {
        cur_errno = ERR_NOTDIR;
        p_perror("umount");
        return;
    }
    mounted = false;
}

/**
 * @brief For each input file, update its timestamp or create it.
 *
 * Takes any number of inputs.
 * If file doesn't exist creates it as a regular file.
 * If file is regular or a directory updates its timestamp.
 * If file is a link the file it points to is touched instead.
 * In particular, if the file the link points to doesn't exist it is created.
 * Prints if a directory along the path specified doesn't exist.
 *
 * @param args[i] Name of file to touch.
 */
void pf_touch(int argc, char** args) {
    if (!mounted) { arg_error2("touch: No filesystem mounted\n"); return; } 
    if (argc == 1) { arg_error2("touch: Missing file operand\n"); return; }
    for (int i = 1; i < argc; i++) {
        char* path = abs_path2(args[i]);
        if (create_file(path, REGULAR_FILE) == -1) { 
            if (errno != EEXIST) { perror("touch"); return; }
        }
        if (write_file(path, 0, NULL, 0, true) == -1) { 
            cur_errno = ERR_PERM;
            p_perror("touch"); return;
        }
    }
}

/**
 * @brief Remove each input file.
 *
 * Takes any number of inputs.
 * Will remove regular files and links but prints an error if input file is a directory.
 * If input is a link removes the link and not the target file.
 *
 * @param args[i] Name of file to remove.
 */
void pf_rm(int argc, char** args) {
    if (!mounted) { arg_error2("rm: No filesystem mounted\n"); return; }
    if (argc == 1) { arg_error2("rm: Missing source file\n"); return; }
    for (int i = 1; i < argc; i++) {
        char* path = abs_path2(args[i]);
        File f = get_file(path, false);
        if (f.type == DIRECTORY_FILE) { cur_errno = ERR_DIR; p_perror("rm"); return; }
        if (truncate_file(path, false) == -1) { 
            cur_errno = ERR_PERM;
            p_perror("rm");  
            return; 
        }
        cleanup_file(remove_file(path)); 
    } 
}

/**
 * @brief Renames source to dest or moves source into dest if dest is a directory.
 *
 * If source doesn't exist an error is printed.
 * If a directory in the path to dest doesn't exist an error is printed.
 * If dest doesn't exist simply renames source to dest.
 * If dest is a file and source is a directory throws an error.
 * If dest and source are files removes dest's data and then renames source to dest.
 * If dest is a link it is NOT followed.
 * If dest is a directory, we instead set dest = path_to_dest/name_of_source 
 * and follow the above rules effectively moving source into dest.
 *
 * @param args[1] Source file.
 * @param args[2] Destination file.
 */
void pf_mv(int argc, char** args) {
    if (!mounted) { arg_error2("mv: No filesystem mounted\n"); return; }
    if (argc == 1) { arg_error2("mv: Missing source file\n"); return; }
    if (argc == 2) { arg_error2("mv: Missing destination file\n"); return; }
    if (argc > 3) { arg_error2("mv: Too many arguments\n"); return; }
    char* p1 = abs_path2(args[1]);
    char* p2 = abs_path2(args[2]);
    File f = get_file(p1, false);
    if (f.name[0] == 0) { cur_errno = ERR_NOENT; p_perror("mv"); return; }
    // Name is same if into directory, new otherwise
    int i = 2;
    File tgt = get_file(p2, false);
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
        if (errno != EEXIST) { perror("mv"); return; }
        File dest = get_file(p2, false);
        if (dest.type != DIRECTORY_FILE && f.type == DIRECTORY_FILE) { 
            cur_errno = ERR_DIR; p_perror("mv"); return;
        }
        if (truncate_file(p2, false) == -1) {
            cur_errno = ERR_PERM;
            p_perror("mv"); 
            return;
        }
    }
    set_file(p2, f, false);
    cleanup_file(remove_file(p1));
}

/**
 * @brief Copies source to dest, each possibly on the host machine.
 *
 * If source doesn't exist prints an error.
 * If a directory on the path to dest doesn't exist prints an error.
 * If source is a directory prints an error.
 * All links are followed.
 * Up to one -h flag is allowed.
 * If dest is a directory, we instead set dest = path_to_dest/name_of_source
 * and follow the above rules effectively copying source into dest.
 *
 * @param args[1] Source file, possibly with preceding -h flag.
 * @param args[2] Destination file, possibly with preceding -h flag.
 */
void pf_cp(int argc, char** args) {
    if (!mounted) { arg_error2("cp: No filesystem mounted\n"); return; }
    if (argc == 1) { arg_error2("cp: Missing source file\n"); return; }
    bool h_src = strcmp(args[1], "-h") == 0;
    if (argc == 2) { 
        if (!h_src) { arg_error2("cp: Missing destination file\n"); return; }
        if (h_src) { arg_error2("cp: Missing source file\n"); return; }
    }
    bool h_dest = strcmp(args[2], "-h") == 0;
    if (argc > 4) { arg_error2("cp: Too many arguments\n"); return; }
    Vec v;
    if (h_src) {
        int src_fd = open(args[2], O_RDONLY);
        if (src_fd == -1) { arg_error2("cp: Cannot open source file\n"); return; }
        struct stat st;
        if (fstat(src_fd, &st) == -1) { arg_error2("cp: Cannot stat source file\n"); return; };
        v.size = st.st_size;
        v.buf = (uint8_t*) malloc(v.size);
        read(src_fd, v.buf, v.size);
        close(src_fd);
    } else {
        v = read_files2(&args[1], 1);
        if (v.size == -1) { cur_errno = ERR_PERM; p_perror("cp"); return; }
    }
    if (h_dest) { 
        int dest_fd = open(args[3], O_WRONLY | O_CREAT | O_TRUNC, 0777); 
        if (dest_fd == -1) { cur_errno = ERR_NOENT; p_perror("cp"); return; }
        write(dest_fd, v.buf, v.size);
        close(dest_fd);
    } else {
        char* path = abs_path2(args[h_src ? 3 : 2]);
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
            if (errno != EEXIST) { perror("cp"); return; }
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

/**
 * @brief Read from a list of files or write to one file.
 *
 * With the -a flag cat appends to a file.
 * With the -w flag cat writes to a file.
 * Following the flag is a single file which is written to.
 * Without a flag cat writes to STDOUT.
 * If there are no files preceding the flag cat reads from STDIN.
 * If there are files preceding the flag cat reads them in order.
 * All links are followed.
 * Cat prints an error if the user tries to read from STDIN and write to STDOUT, 
 * either source or dest is a directory, a source file doesn't exist, or if
 * a directory along the path to the destination file doesn't exist.
 *
 * @param args[i] Optional list of source files to read from.
 * @param args[argc-2] Optional append/write flag.
 * @param args[argc-1] Optional output file.
 */
void pf_cat(int argc, char** args) {
    if (!mounted) { arg_error2("cat: No filesystem mounted\n"); return; }
    if (argc == 1) { arg_error2("cat: Missing file operand\n"); return; }
    char* flag = args[argc - 2];
    bool append_f = strcmp(flag, "-a") == 0;
    bool write_f = strcmp(flag, "-w") == 0;
    Vec vec;
    if (argc == 3 && (append_f || write_f)) {
        vec.buf = (uint8_t*) malloc(MAX_LINE_LENGTH);
        vec.size = read(STDIN_FILENO, vec.buf, MAX_LINE_LENGTH);
    } else {
        vec = read_files2(&args[1], (append_f || write_f) ? argc - 3 : argc - 1);
        if (vec.size == -1) { cur_errno = ERR_PERM; p_perror("cat"); return; };
    }
    if (append_f) {
        char* path = abs_path2(args[argc - 1]);
        if (create_file(path, REGULAR_FILE) == -1) {
            if (errno != EEXIST) { perror("cat"); return; }
            File f = get_file(path, true);
            if (f.name[0] != 0 && f.type == DIRECTORY_FILE) {
                cur_errno = ERR_DIR;
                p_perror("cat");
            }
        }
        if (write_file(path, get_file(path, true).size, vec.buf, vec.size, true)) {
            cur_errno = ERR_PERM;
            p_perror("cat");
            return;
        }
    } else if (write_f) {
        char* path = abs_path2(args[argc - 1]);
        if (create_file(path, REGULAR_FILE) == -1)  {
            if (errno != EEXIST) { perror("cat"); return; }
            if (truncate_file(path, true) == -1) {
                cur_errno = ERR_PERM;
                p_perror("cat");
                return;
            }
        }
        if (write_file(path, 0, vec.buf, vec.size, true) == -1) {
            cur_errno = ERR_PERM;
            p_perror("cat"); return;
        }
    } else {
        write(STDOUT_FILENO, vec.buf, vec.size);
    }
}

/**
 * @brief List the files in a directory.
 *
 * Without an argument ls lists files in the current directory.
 * With an absolute or relative path ls lists files in that directory.
 * If the argument does not lead to a directory ls prints an error.
 *
 * @param args[1] Optional absolute or relative path to a directory.
 */
void pf_ls(int argc, char** args)  {
    if (!mounted) { arg_error2("ls: No filesystem mounted\n"); return; }
    if (argc > 2) { arg_error2("ls: Too many arguments\n"); return; }
    char* path;
    if (argc == 2) {
        path = abs_path2(args[1]);
    } else {
        path = abs_path2("");
    }
    File* list = list_directory(path); 
    if (list == NULL) { cur_errno = ERR_PERM; p_perror("ls"); return; }
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
        struct tm *time_data = localtime(&list[i].mtime);
        printf("%*hu %c %c%c%c %*u %s %*u %02u:%02u %.*s\n", 
            fb_len, list[i].first_block, 
            list[i].type == UNKNOWN_FILE ? 'u' : list[i].type == REGULAR_FILE ? 'f' : list[i].type == DIRECTORY_FILE ? 'd' : 'l',
            list[i].perm & EXECUTE_PERM ? 'x' : '-', 
            list[i].perm & READ_PERM ? 'r' : '-',
            list[i].perm & WRITE_PERM ? 'w' : '-',
            size_len, list[i].size, 
            MONTHS2[time_data->tm_mon],
            day_len, time_data->tm_mday,
            time_data->tm_hour,
            time_data->tm_min,
            name_len, list[i].name
        );
    }
}

/**
 * @brief Change the permissions of a file.
 *
 * Can add/remove one of read/write/execute on a file.
 * Links are always followed.
 * Prints an error if the permissions modifier is invalid or
 * if the file cannot be found.
 *
 * @param args[1] (+/-)(r/w/x) permissions modifier.
 * @param args[2] File to change permissions of.
 */
void pf_chmod(int argc, char** args) {
    if (!mounted) { arg_error2("chmod: No filesystem mounted\n"); return; }
    if (argc <= 2) { arg_error2("chmod: Missing file operand\n"); return; }
    if (argc > 3) { arg_error2("chmod: Too many arguments\n"); return; }
    File f;
    int perm;
    if (args[1][1] == 'x') { 
        perm = EXECUTE_PERM;
    } else if (args[1][1] == 'r') {
        perm = READ_PERM;
    } else if (args[1][1] == 'w') {
        perm = WRITE_PERM;
    } else {
        arg_error2("chmod: Invalid permssions modifier\n");
        return;
    }
    char* path = abs_path2(args[2]);
    f = get_file(path, true);
    if (f.name[0] == 0) { cur_errno = ERR_NOENT; p_perror("chmod"); return; }
    if (args[1][0] == '-') { 
        f.perm &= (0x7 - perm);
    } else if (args[1][0] == '+') {
        f.perm |= perm;
    } else {
        arg_error2("chmod: Invalid permssions modifier\n");
        return;
    }
    set_file(path, f, true);
}

/**
 * @brief Change the current working directory.
 *
 * If the path does not lead to a directory cd prints an error.
 * Dot and dot-dot supported.
 *
 * @param args[1] Absolute or relative path to a directory.
 */
void pf_cd(int argc, char** args) {
    if (argc == 1) { arg_error2("cd: Missing operand\n"); return; }
    if (argc > 2) { arg_error2("cd: Too many arguments\n"); return; }
    char* path = abs_path2(args[1]);
    File f = get_file(path, true);
    if (f.name[0] == 0) { cur_errno = ERR_NOENT; p_perror("cd"); return; }
    if (f.type != DIRECTORY_FILE) { cur_errno = ERR_NOTDIR; p_perror("cd"); return; }
    pwd2 = path;
}

/**
 * @brief Make a directory.
 *
 * Any number of arguments supported.
 * If the file exists an error is printed.
 * If a directory along the path doesn't exist an error is printed.
 *
 * @param args[i] Absolute or relative path to a directory.
 */
void pf_mkdir(int argc, char** args) {
    if (!mounted) { arg_error2("mkdir: No filesystem mounted\n"); return; } 
    if (argc == 1) { arg_error2("mkdir: Missing operand\n"); return; }
    for (int i = 1; i < argc; i++) {
        char* path = abs_path2(args[i]);
        if (create_file(path, DIRECTORY_FILE) == -1) { 
            cur_errno = ERR_PERM;
            p_perror("mkdir"); return; 
        }
    }
}

/**
 * @brief Remove a directory.
 *
 * Any number of arguments supported.
 * If the file doesn't exist or is not a directory an error is printed.
 * If the directory is not empty an error is printed.
 * Links are not followed.
 *
 * @param args[i] Absolute or relative path to a directory.
 */
void pf_rmdir(int argc, char** args) {
    if (!mounted) { arg_error2("rmdir: No filesystem mounted\n"); return; }
    if (argc == 1) { arg_error2("rmdir: Missing operand\n"); return; }
    for (int i = 1; i < argc; i++) {
        char* path = abs_path2(args[i]);
        File f = get_file(path, false);
        if (f.type != DIRECTORY_FILE) { cur_errno = ERR_NOTDIR; p_perror("rmdir"); return; }
        if (truncate_file(path, false) == -1) { 
            cur_errno = ERR_NOTDIR;
            p_perror("rmdir"); 
            return; 
        }
        cleanup_file(remove_file(path));
    }
}

/**
 * @brief Print the current working directory.
 */
void pf_pwd(int argc, char** args) {
    if (argc > 1) { arg_error2("pwd: Too many arguments\n"); return; }
    if (!mounted) { arg_error2("pwd: No filesystem mounted\n"); return; }
    if (strlen(pwd2) > 0) {
        printf("%s\n", pwd2);
    } else {
        printf("%s\n", "/");
    }
}

/**
 * @brief Create a link to a file.
 *
 * Only symbolic links are supported.
 * Target file doesn't need to exist.
 * If link name already exists as a file prints an error.
 * If a directory on the link name path doesn't exist prints an error.
 *
 * @param args[1] Mandatory symbolic `-s` flag.
 * @param args[2] Target file.
 * @param args[3] Link name.
 */
void pf_ln(int argc, char** args) {
    if (!mounted) { cur_errno = ERR_PERM; arg_error2("ln: No filesystem mounted\n"); return; }
    if (argc <= 2) { cur_errno = ERR_INVAL; arg_error2("ln: Missing target file\n"); return; }
    if (strcmp(args[1], "-s") != 0) { cur_errno = ERR_PERM; arg_error2("ln: Hard links not supported\n"); return; }
    if (argc == 3) { cur_errno = ERR_INVAL; arg_error2("ln: Missing link name\n"); return; }
    if (argc > 4) { cur_errno = ERR_INVAL; arg_error2("ln: Too many arguments\n"); return; }
    char* path = abs_path2(args[3]);
    File file = get_file(path, false);
    if (file.name[0] != 0) {
        cur_errno = ERR_PERM; p_perror("ln"); return;
    }
    if (create_file(path, LINK_FILE) == -1) { 
        cur_errno = ERR_PERM;
        p_perror("ln"); return;
    }
    char* target = abs_path2(args[2]);
    if (write_file(path, 0, (uint8_t*) target, strlen(target) + 1, false) == -1) {
        cur_errno = ERR_PERM;
        p_perror("ln");
        return;
    }
}

/**
 * @brief Main loop.
 */
int main(int _argc, char** _argv) {
    while (true) {
        // Write prompt
        if (write(STDOUT_FILENO, PROMPT, strlen(PROMPT)) == -1){
            cur_errno = ERR_PERM;
            p_perror("write");
            exit(EXIT_FAILURE);
        }
        // Read user input
        char input_line[MAX_LINE_LENGTH + 1];
        int num_chars = read(STDIN_FILENO, input_line, MAX_LINE_LENGTH);
        if (num_chars == -1){
            cur_errno = ERR_PERM;
            p_perror("read");
            exit(EXIT_FAILURE);
        }
        input_line[num_chars] = '\0';
        // Create temp array
        int len = strlen(input_line);
        char tmp[len + 1];
        memcpy(tmp, input_line, len + 1);
        // Compute number of args (maybe too many?)
        int argc = 0;
        if (strtok(tmp, " \n\t")) { argc++; }
        while (strtok(NULL, " \n\t")) { argc++; }
        if (argc == 0) { continue; }
        // Build array of arguments
        char** args = malloc(sizeof(char*) * argc);
        args[0] = strtok(input_line, " \n\t");
        for (int i = 1; i < argc; i++) { 
            args[i] = strtok(NULL, " \n\t");
        }
        // Call correct function
        if (strcmp(args[0], "mkfs") == 0) { pf_mkfs(argc, args); } 
        else if (strcmp(args[0], "mount") == 0) { pf_mount(argc, args); }
        else if (strcmp(args[0], "umount") == 0) { pf_umount(argc, args); }
        else if (strcmp(args[0], "touch") == 0) { pf_touch(argc, args); }
        else if (strcmp(args[0], "rm") == 0) { pf_rm(argc, args); }
        else if (strcmp(args[0], "mv") == 0) { pf_mv(argc, args); }
        else if (strcmp(args[0], "cp") == 0) { pf_cp(argc, args); }
        else if (strcmp(args[0], "cat") == 0) { pf_cat(argc, args); }
        else if (strcmp(args[0], "ls") == 0) { pf_ls(argc, args); }
        else if (strcmp(args[0], "chmod") == 0) { pf_chmod(argc, args); } 
        else if (strcmp(args[0], "cd") == 0) { pf_cd(argc, args); } 
        else if (strcmp(args[0], "mkdir") == 0) { pf_mkdir(argc, args); }
        else if (strcmp(args[0], "rmdir") == 0) { pf_rmdir(argc, args); } 
        else if (strcmp(args[0], "pwd") == 0) { pf_pwd(argc, args); }
        else if (strcmp(args[0], "ln") == 0) { pf_ln(argc, args); } 
        else { arg_error2("pennfat: Command not recognized\n"); }
    }
}