#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#include "filesys.h"
#include "../error.h"

/**
 * @file filesys.c
 * @brief An implementation of the filesystem API.
 */

/**
 * @brief Indicates a block is free in the FAT.
 */
#define FREE_BLOCK 0x0000

/**
 * @brief Indicates the last block of a file in the FAT.
 */
#define LAST_BLOCK 0xFFFF

/**
 * @brief Indicates that links should not be followed.
 */
#define SKIP_NONE 0x00

/**
 * @brief Indicates that links should be followed until the next link leads to a non-existent file.
 */
#define SKIP_TO_LAST 0x01

/**
 * @brief Indicates that all links should be followed.
 */
#define SKIP_ALL 0x02


/**
 * @brief Filesystem file descriptor on host.
 */
int fs_fd;

/**
 * @brief Memory-mapped FAT.
 */
uint16_t* fat;

/**
 * @brief Block size in bytes.
 */
int block_size;

/**
 * @brief Number of blocks in FAT.
 */
int fat_blocks;

/**
 * @brief Number of data blocks. 
 *
 * 1-indexed in FAT as `fat[0]` stores filesystem configuration information.
 * Maximum index is `LAST_BLOCK - 1 = 0xfffe`.
 */
int data_blocks;

/**
 * @brief A directory entry struct type.
 */
typedef struct entry {
    /**
    * @brief A file found in the directory.
    */
    File file;
    /**
    * @brief Physical offset of file in fs_fd.
    */
    int position;
} Entry;

/**
 * @brief Entry representing the root directory.
 */
Entry root;

/**
 * @brief Entry representing EOD.
 */
Entry eod = (Entry) { (File) { "", 0, 0, UNKNOWN_FILE, 0, 0 }, -1 };

/**
 * @brief An absolute path struct type.
 */
typedef struct path {
    /**
    * @brief Sequence of nested directories leading to file location.
    *
    * A NULL pointer follows the last entry.
    * In particular, if the first entry is NULL we are in the root directory.
    */
    char** dir;
    /**
    * @brief File name.
    *
    * Only NULL to indicate the empty path, i.e. the path to the root directory.
    */
    char* name;
} Path;

/**
 * @brief Split a parsed absolute path string into a Path struct type.
 *
 * Dot and dot-dot are not supported.
 * @return A path struct.
 * @param name A parsed absolute path string.
 */
Path split_path(char* path_str) {
    // Compute number of tokens
    char tmp[strlen(path_str) + 1];
    strcpy(tmp, path_str);
    int argc = 0;
    if (strtok(tmp, "/")) { argc++; }
    while (strtok(NULL, "/")) { argc++; }
    // Copy to not change original
    char* copy_str = (char*) malloc(strlen(path_str) + 1);
    strcpy(copy_str, path_str);
    // Build token sequence
    char** dir = malloc(sizeof(char*) * argc);
    dir[0] = strtok(copy_str, "/");
    for (int i = 1; i < argc; i++) { 
        dir[i] = strtok(NULL, "/");
    }
    char* name;
    if (argc > 0) {
        name = dir[argc-1];
        dir[argc-1] = NULL;
    } else {
        name = NULL;
    }
    return (Path) { dir, name };
}

/**
 * @brief Reserve and return a block to follow `block` in the FAT.
 *
 * If block is nonzero sets `fat[block] = new_block`.
 * Zeroes out any newly allocated memory (directory assumes this!)
 * Note: need to call fsync(fs_fd) after any fat write.
 * Throws an error if no space left.
 *  
 * @return The new block on success or -1 on failure.
 * @param block The block to extend a file from or 0 to simply reserve a block.
 */
int extend_data(int block) {
    for (int i = 1; i <= data_blocks; ++i) {
        if (fat[i] == FREE_BLOCK) {
            if (block != 0) {
                fat[block] = i;
            }
            fat[i] = LAST_BLOCK;
            fsync(fs_fd);
            if (lseek(fs_fd, (i + fat_blocks - 1) * block_size, SEEK_SET) == -1) {
                cur_errno = ERR_PERM;
                p_perror("lseek");
                exit(EXIT_FAILURE);
            }
            uint8_t* zeroes = (uint8_t*) calloc(block_size, 1);
            if (write(fs_fd, zeroes, block_size) == -1) {
                cur_errno = ERR_PERM;
                p_perror("write");
                exit(EXIT_FAILURE);
            }
            return i;
        }
    }
    errno = ENOSPC;
    return 0;
}

/**
 * @brief Get position which is logical offset bytes ahead of input position.
 *
 * Terminology: position => physical location in fs_fd, offset => number of logical bytes.
 * If the end of the current file is reached seek_data extends the file.
 * Input logical offset must be non-negative.
 * Throws an error if no space left.
 *  
 * @return The position which is reached or -1 on failure.
 * @param position The position to begin from.
 * @param offset Logical offset size.
 */
int seek_data(int position, int offset) {
    int block = position / block_size;
    offset += position % block_size;
    while (1) {
        if (offset < block_size) {
            return block * block_size + offset;
        }
        offset -= block_size;
        if (fat[block] == LAST_BLOCK) {
            block = extend_data(block);
            if (block == 0) { return -1; }
        } else {
            block = fat[block];
        }
    }
}

/**
 * @brief Write `size` data from `buf` beginning at `position`.
 *
 * Written in logically contiguous manner beginning at position.
 * Will extend file if `LAST_BLOCK` is reached.
 * Throws an error if no space left.
 *  
 * @return -1 on failure and 0 on success.
 * @param position The position to begin from.
 * @param buf Buffer to write from.
 * @param size Number of bytes to write.
 */
int write_data(int position, uint8_t* buf, int size) {
    int block = position / block_size;
    int offset = position % block_size;
    int i = 0;
    while (1) {
        lseek(fs_fd, (block + fat_blocks - 1) * block_size + offset, SEEK_SET);
        if (size - i <= block_size - offset) {
            if (write(fs_fd, &buf[i], size - i) == -1) {
                cur_errno = ERR_PERM;
                p_perror("write");
                exit(EXIT_FAILURE);
            }
            fsync(fs_fd);
            return 0;
        }
        if (write(fs_fd, &buf[i], block_size - offset) == -1) {
            cur_errno = ERR_PERM;
            p_perror("write");
            exit(EXIT_FAILURE);
        }
        i += block_size - offset;
        offset = 0;
        if (fat[block] != LAST_BLOCK) {
            block = fat[block];
        } else {
            block = extend_data(block);
            if (block == 0) { return -1; }
        }
    }
}

/**
 * @brief Reads `size` bytes into `buf`.
 *
 * Read in logically contiguous manner beginning at position.
 *  
 * @return Number of bytes read on success and -1 on failure.
 * @param position The position to begin from.
 * @param buf Buffer to write from.
 * @param size Number of bytes to write.
 */
int read_data(int position, uint8_t* buf, int size) {
    int block = position / block_size;
    int offset = position % block_size;
    int i = 0;
    while (block != LAST_BLOCK) {
        lseek(fs_fd, (block + fat_blocks - 1) * block_size + offset, SEEK_SET);
        if (size - i <= block_size - offset) {
            if (read(fs_fd, &buf[i], size - i) == -1) {
                cur_errno = ERR_PERM;
                p_perror("read");
                exit(EXIT_FAILURE);
            }
            fsync(fs_fd);
            return size;
        }
        if (read(fs_fd, &buf[i], block_size - offset) == -1) {
            cur_errno = ERR_PERM;
            p_perror("read");
            exit(EXIT_FAILURE);
        }
        i += block_size - offset;
        offset = 0;
        block = fat[block];
    }
    return i;
}

/**
 * @brief Free data blocks in logically contiguous manner beginning at block.
 *
 * @param block The block to begin freeing from.
 */
void truncate_data(int block) {
    while (block != LAST_BLOCK) {
        int tmp = block;
        block = fat[block];
        fat[tmp] = FREE_BLOCK;
        fsync(fs_fd);
    }
}

/**
 * @brief Find size of directory with entries beginning at `block`.
 *
 * Note `block` is FAT-addressed so there is an additional offset due to the FAT table.
 * That is, block 1 corresponds to the first block after the FAT table and so on.
 * Size includes deleted files and one EOD file.
 *
 * @return The number of file slots in the directory including the EOD file.
 * @param block The first block containing directory entries.
 */
int space_directory(int block) {
    File f;
    int files_per_block = block_size / 64;
    int count = 0;
    while (1) { 
        for (int i = 0; i < files_per_block; i++) {
            if (lseek(fs_fd, (block + fat_blocks - 1) * block_size + 64 * i, SEEK_SET) == -1) {
                cur_errno = ERR_PERM;
                p_perror("lseek");
                exit(EXIT_FAILURE);
            }
            if (read(fs_fd, &f, sizeof(File)) == -1) {
                cur_errno = ERR_PERM;
                p_perror("read");
                exit(EXIT_FAILURE);
            }
            ++count;
            if (f.name[0] == EOD_FLAG) { 
                return count;
            }
        }
        block = fat[block];
    }
}

/**
 * @brief Enumerate all file slots in a directory beginning at `block`.
 *
 * File slots include deleted files.
 *
 * @return A sequence of file slots terminated by an EOD file.
 * @param block The first block containing directory entries.
 */
Entry* enum_directory(int block) {
    File f;
    Entry* entries = malloc(space_directory(block) * sizeof(Entry));
    int files_per_block = block_size / 64;
    int count = 0;
    while (1) { 
        for (int i = 0; i < files_per_block; i++) {
            if (lseek(fs_fd, (block + fat_blocks - 1) * block_size + 64 * i, SEEK_SET) == -1) {
                cur_errno = ERR_PERM;
                p_perror("lseek");
                exit(EXIT_FAILURE);
            }
            if (read(fs_fd, &f, sizeof(File)) == -1) {
                cur_errno = ERR_PERM;
                p_perror("read");
                exit(EXIT_FAILURE);
            }
            entries[count] = (Entry) { f, (block + fat_blocks - 1) * block_size + 64 * i };
            if (f.name[0] == EOD_FLAG) { 
                return entries;
            }
            ++count;
        }
        block = fat[block];
    }
}

// Declare here because find_file and find_directory call each other
Entry find_directory(char** dir);

/**
 * @brief Find file `name` in directory beginning at `block`.
 *
 * If name is empty we return the first deleted or EOD entry we encounter.
 * If name is nonempty we search for a file with the given name.
 * Returns an EOD file if the file is not found.
 * If `skip_flag` is `SKIP_ALL` we recurse upon finding a link file.
 * If `skip_flag` is `SKIP_NONE` we return link files immedaitely.
 * If `skip_flag` is `SKIP_TO_LAST` we recurse unless the link points to a non-existant file.
 *
 * @return The requested file or an EOD file if it is not found.
 * @param name The name of the file.
 * @param block The first block containing entries in the directory to search.
 * @param skip_flag A macro indicating how to handle link files.
 */
Entry find_file(char* name, int block, int skip_flag) {
    if (name == NULL) { return root; }
    Entry* entries = enum_directory(block);
    int i;
    for (i = 0; entries[i].file.name[0] != EOD_FLAG; ++i) {
        if (name[0] == EOD_FLAG && (
            entries[i].file.name[0] == EOD_FLAG ||
            entries[i].file.name[0] == CLEANED_FLAG ||
            entries[i].file.name[0] == REMOVED_FLAG
        )) { return entries[i]; }
        if (name[0] != EOD_FLAG && strcmp(entries[i].file.name, name) == 0) { 
            if (entries[i].file.type == LINK_FILE && skip_flag != SKIP_NONE) {
                char* next_str = (char*) malloc(entries[i].file.size + 1);
                read_data(block_size * entries[i].file.first_block, (uint8_t*) next_str, entries[i].file.size);
                next_str[entries[i].file.size] = '\0';
                Path path = split_path(next_str);
                Entry d = find_directory(path.dir);
                if (d.file.name[0] == EOD_FLAG || d.file.type != DIRECTORY_FILE) { return entries[i]; }
                Entry e = find_file(path.name, d.file.first_block, skip_flag);
                if (skip_flag == SKIP_ALL || e.file.name[0] != EOD_FLAG) {
                    entries[i] = e;
                }
            }
            return entries[i]; 
        }
    }
    return entries[i];
}

/**
 * @brief Initialize an empty file with given name and type.
 *
 * If creating a directory we also allocate a block for it to maintain
 * the invariant that every directory ends at an EOD file.
 * Throws an error if no space left.
 *
 * @return The newly created file or an EOD file if no space left.
 * @param name The name of the file.
 * @param type The file type, see macros in filesys.h
 */
File init_file(char* name, uint8_t type) {
    File f;
    memcpy(f.name, name, 32);
    // Zero out garbage in name
    int i = strlen(name);
    if (i > 31) { i = 31; }
    for (; i < 32; i++) {
        f.name[i] = '\0';
    }
    // Set other fields
    f.size = 0;
    f.first_block = LAST_BLOCK;
    f.type = type;
    f.perm = (type == DIRECTORY_FILE) ? EXECUTE_PERM | READ_PERM | WRITE_PERM : READ_PERM | WRITE_PERM;
    time(&f.mtime);
    if (f.type == DIRECTORY_FILE) {
        f.first_block = extend_data(0);
        if (f.first_block == 0) { 
            return find_file("", root.file.first_block, SKIP_ALL).file;
        }
    }
    return f;
}

/**
 * @brief Add file `f` to the directory beginning at `block`.
 *
 * Important invariant: directory is always terminated by an EOD file.
 * Throws an error if no space left.
 *
 * @return Position of newly added file in fs_fd or -1 on failure.
 * @param f The file to add.
 * @param block The first block containing entries in the directory.
 */
int add_file(File f, int block) {
    Entry e = find_file("", block, SKIP_ALL);
    block = e.position / block_size;
    int offset = e.position % block_size;
    // Push if filling last slot of last block
    if ((offset + 64) % block_size == 0 && fat[block] == LAST_BLOCK) {
        int b = extend_data(block);
        if (b == 0) { return -1; }
    }
    //printf("writing file %s to %x\n", f.name, e.position);
    if (lseek(fs_fd, e.position, SEEK_SET) == -1){
        cur_errno = ERR_PERM;
        p_perror("lseek");
        exit(EXIT_FAILURE);
    }
    if (write(fs_fd, &f, sizeof(File)) == -1) {
        cur_errno = ERR_PERM;
        p_perror("write");
        exit(EXIT_FAILURE);
    }
    fsync(fs_fd);
    return e.position;
}

/**
 * @brief Find entry corresponding to the sequence of nested directories `dir`.
 *
 * If `dir[0] = NULL` this indicates an empty path and we return `root`.
 * Returns immediately on error if can't find entry, entry is not a directory,
 * or entry doesn't have execute permissions.
 * Sets errno appropriately.
 *
 * @return The entry of the directory or an EOD file on error.
 * @param dir A null terminated sequence of nested directories.
 */
Entry find_directory(char** dir) {
    if (dir[0] == NULL) { return root; }
    int block = 1;
    for (int i = 0;; ++i) {
        Entry e = find_file(dir[i], block, SKIP_ALL);
        if (e.file.name[0] == EOD_FLAG ) { errno = ENOENT; return eod; }
        if (e.file.type != DIRECTORY_FILE) { errno = ENOTDIR; return eod;}
        if ((e.file.perm & EXECUTE_PERM) == 0) { errno = EACCES; return eod; }
        if (dir[i+1] == NULL) { return e; }
        block = e.file.first_block;
    }
}

/**
 * @brief Initialize filesystem given by `fs` string with given config information.
 *
 * Set `new_block_size = 2^{8 + new_block_size_config}`.
 * FAT takes up `new_fat_blocks * block_size` bytes.
 * Then `new_fat_entries` is this divided by 2 minus 1 entries (each block pointer is 2 bytes, first slot is config info).
 * Then the data region has size `new_fat_entries * block_size` bytes unless `new_fat_entries >= LAST_BLOCK`
 * in which case we cap the size of the data region as `LAST_BLOCK - 1`.
 * Throws an error if fs refers to the currently open filesystem.
 *
 * @return -1 on failure and 0 on success.
 * @param fs The name of the file to contain the filesystem on the host machine.
 * @param new_fat_blocks The number of fat blocks in the new filesystem.
 * @param new_block_size_config A value representing the block size of the filesystem.
 */
int init_fs(char* fs, int new_fat_blocks, int new_block_size_config) {
    // Check files aren't same
    int new_fs_fd = open(fs, O_RDWR | O_CREAT | O_APPEND | O_SYNC, 0666);
    if (new_fs_fd == -1) {
        return -1;
    }
    struct stat stat1, stat2;
    if(fstat(fs_fd, &stat1) < 0) { return -1; }
    if(fstat(new_fs_fd, &stat2) < 0) { return -1; }
    if ((stat1.st_dev == stat2.st_dev) && (stat1.st_ino == stat2.st_ino)) { errno = EBUSY; return -1; }
    close(new_fs_fd);
    new_fs_fd = open(fs, O_RDWR | O_CREAT | O_TRUNC | O_SYNC, 0666);
    if (new_fs_fd == -1) {
        return -1;
    }
    // Compute config
    int new_block_size = 1 << (new_block_size_config + 8);
    int new_data_blocks = (new_block_size * new_fat_blocks / 2) - 1;
    if (new_data_blocks >= LAST_BLOCK) { new_data_blocks = LAST_BLOCK - 1; }
    uint16_t init_vals[2];
    // Blocks in fat is MSB, block size config is LSB
    init_vals[0] = (uint16_t)((new_fat_blocks << 8) | new_block_size_config);
    // Block 1 is first and last block of directory
    init_vals[1] = LAST_BLOCK;
    if (write(new_fs_fd, init_vals, 4) == -1) {
        cur_errno = ERR_PERM;
        p_perror("write");
        exit(EXIT_FAILURE);
    }
    // Set rest of new filesystem to zeroes: seek to end - 1 and then write a byte
    if (lseek(new_fs_fd, (new_fat_blocks + new_data_blocks) * new_block_size - 1, SEEK_SET) == -1) {
        cur_errno = ERR_PERM;
        p_perror("lseek");
        exit(EXIT_FAILURE);
    }
    uint8_t zero = 0;
    if (write(new_fs_fd, &zero, 1) == -1) {
        cur_errno = ERR_PERM;
        p_perror("write");
        exit(EXIT_FAILURE);
    }
    if (close(new_fs_fd) == -1) {
        cur_errno = ERR_PERM;
        p_perror("close");
        exit(EXIT_FAILURE);
    }
    return 0;
}

/**
 * @brief Mount filesystem given by `fs` string.
 *
 * Fails if the file cannot be opened.
 * Initializes lots of global variables such as `fat` and the config information.
 *
 * @return -1 on failure and 0 on success.
 * @param fs The name of the file containing the filesystem on the host machine to mount.
 */
int mount_fs(char* fs) {
    fs_fd = open(fs, O_RDWR);
    if (fs_fd == -1) { return -1; }
    uint16_t config;
    read(fs_fd, &config, sizeof(uint16_t));
    // MSB (little endian bottom byte) encodes block size
    block_size = 1 << (8 + (config & 0xFF));
    // LSB (little endian top byte) encodes number of fat blocks
    fat_blocks = config >> 8;
    data_blocks = (block_size * fat_blocks / 2) - 1;
    if (data_blocks >= LAST_BLOCK) { data_blocks = LAST_BLOCK - 1; }
    fat = mmap(NULL, fat_blocks * block_size, PROT_READ | PROT_WRITE, MAP_SHARED, fs_fd, 0);
    root = (Entry) { (File) { "root", 0, 1, DIRECTORY_FILE, READ_PERM | WRITE_PERM | EXECUTE_PERM, 0 }, -1 };
    return 0;
}

/**
 * @brief Unmount the currently mounted filesystem.
 *
 * @return -1 on failure and 0 on success.
 */
int unmount_fs() {
    if (close(fs_fd) == -1) {
        cur_errno = ERR_PERM;
        p_perror("close");
        exit(EXIT_FAILURE);
    }
    return 0;
}

/**
 * @brief Creates file at `path_str` and type `type` if it doesn't already exist.
 *
 * If a directory along the path to the file cannot be found throws an `ENOENT` error.
 * If a token along the path to the file is not a directory throws an `ENOTDIR` error.
 * On any permissions error throws `EACCES`. Directory needs write permissions.
 * On success, directory `size` containing file is increased and `mtime` is updated.
 * If the file is not a link and already exists or is a link and points to an existing file throws an `EEXIST` error.
 * If the file is a link and points to a file which doesn't exist that file will be created instead as a regular file.
 *
 * @return -1 on failure and 0 on success.
 * @param path_str Path to create file at.
 * @param path_str Type of file to create.
 */
int create_file(char* path_str, uint8_t type) {
    Path path = split_path(path_str);
    Entry d = find_directory(path.dir); // this might not be the directory containing e!
    if (d.file.name[0] == EOD_FLAG) { return -1; }
    if ((d.file.perm & WRITE_PERM) == 0) { errno = EACCES; return -1; }
    Entry e = find_file(path.name, d.file.first_block, SKIP_TO_LAST);
    if (e.file.type == LINK_FILE) { // followed links and found dead end
        char* new_name = (char*) malloc(e.file.size + 1);
        read_data(block_size * e.file.first_block, (uint8_t*) new_name, e.file.size);
        return create_file(new_name, REGULAR_FILE); // create this file
    } else if (e.file.name[0] != EOD_FLAG) { 
        errno = EEXIST; 
        return -1; 
    }
    d.file.size += 64;
    time(&d.file.mtime);
    if (d.position >= 0) { // not root
        if (lseek(fs_fd, d.position, SEEK_SET) == -1) {
            cur_errno = ERR_PERM;
            p_perror("lseek");
            exit(EXIT_FAILURE);
        }
        if (write(fs_fd, &d.file, sizeof(File)) == -1) {
            cur_errno = ERR_PERM;
            p_perror("write");
            exit(EXIT_FAILURE);
        }
    }
    File f = init_file(path.name, type);
    if (f.name[0] == EOD_FLAG) { return -1; }
    if (add_file(f, d.file.first_block) == -1) { return -1; }
    return 0;
}

/**
 * @brief Sets file metadata to `f` in directory entry at `path_str`.
 *
 * If the file cannot be located throws an `ENOENT` or `ENOTDIR` error.
 * On any permissions error throws `EACCES`. 
 * If `skip_flag` is false and `path_str` leads to a link the link will have its own metadata set.
 * If instead `skip_flag` is true the file pointed to by such a link will be set.
 *
 * @return -1 on failure and 0 on success.
 * @param path_str Path to file to set metadata of.
 * @param f New metadata entry which will be set.
 * @param skip_flag If the target file is a link, should it be followed?
 */
int set_file(char* path_str, File f, bool skip_flag) {
    Path path = split_path(path_str);
    Entry d = find_directory(path.dir);
    if (d.file.name[0] == EOD_FLAG) { return -1; }
    Entry e = find_file(path.name, d.file.first_block, skip_flag ? SKIP_ALL : SKIP_NONE);
    if (e.file.name[0] == EOD_FLAG) { errno = ENOENT; return -1; }
    if (lseek(fs_fd, e.position, SEEK_SET) == -1) {
        cur_errno = ERR_PERM;
        p_perror("lseek");
        exit(EXIT_FAILURE);
    }
    if (write(fs_fd, &f, sizeof(File)) == -1) {
        cur_errno = ERR_PERM;
        p_perror("write");
        exit(EXIT_FAILURE);
    }
    return 0;
}

/**
 * @brief Gets file metadata from directory entry at `path_str`.
 *
 * If the file cannot be located throws an `ENOENT` or `ENOTDIR` error.
 * On any permissions error throws `EACCES`. 
 * If `skip_flag` is false and `path_str` leads to a link the link's metadata is returned.
 * If instead `skip_flag` is true the file pointed to by such a link will have its metadata returned.
 *
 * @return An EOD entry on failure and the file metadata on success.
 * @param path_str Path to file to get metadata from.
 * @param skip_flag If the target file is a link, should it be followed?
 */
File get_file(char* path_str, bool skip_flag) {
    Path path = split_path(path_str);
    Entry d = find_directory(path.dir);
    if (d.file.name[0] == EOD_FLAG) { return d.file; }
    Entry e = find_file(path.name, d.file.first_block, skip_flag ? SKIP_ALL : SKIP_NONE);
    if (e.file.name[0] == EOD_FLAG) { errno = ENOENT; }
    return e.file;
}

/**
 * @brief Reads `size` bytes into `buf` beginning at `offset` in file located at `path_str`.
 *
 * If the file cannot be located throws an `ENOENT` or `ENOTDIR` error.
 * If the file is a directory throws an `EISDIR` error.
 * On any permissions error throws `EACCES`. File needs read permissions.
 * Extends file if offset goes beyond current size.
 *
 * @return The number of bytes read on success and -1 on failure.
 * @param path_str Path to file to read from.
 * @param offset Logical offset to begin reading from in the file.
 * @param buf Buffer to read data into.
 * @param size Number of bytes to read.
 */
int read_file(char* path_str, int offset, uint8_t* buf, int size) {
    Path path = split_path(path_str);
    Entry d = find_directory(path.dir);
    if (d.file.name[0] == EOD_FLAG) { return -1; }
    Entry e = find_file(path.name, d.file.first_block, SKIP_ALL);
    if (e.file.name[0] == EOD_FLAG) { errno = ENOENT; return -1; }
    if (e.file.type == DIRECTORY_FILE) { errno = EISDIR; return -1; }
    if ((e.file.perm & READ_PERM) == 0) { errno = EACCES; return -1; }
    e.position = seek_data(block_size * e.file.first_block, offset);
    if (e.position == -1) { return -1; }
    return read_data(e.position, buf, size);
}

/**
 * @brief Writes `size` bytes from `buf` beginning at `offset` into file located at `path_str`.
 *
 * If the file cannot be located throws an `ENOENT` or `ENOTDIR` error.
 * If the file is a directory throws an `EISDIR` error.
 * On any permissions error throws `EACCES`. File and directory need write permissions.
 * Updates `mtime`, `size`, and possibly `first_block` fields of file metadata.
 * Also throws an error if no space left.
 *
 * @return The number of bytes written on success and -1 on failure.
 * @param path_str Path to file to write to.
 * @param offset Logical offset to begin writing into the file from.
 * @param buf Buffer to write data from.
 * @param size Number of bytes to write.
 * @param skip_flag If the target file is a link, should it be followed?
 */
int write_file(char* path_str, int offset, uint8_t* buf, int size, bool skip_flag) {
    Path path = split_path(path_str);
    Entry d = find_directory(path.dir);
    if (d.file.name[0] == EOD_FLAG) { return -1; }
    if ((d.file.perm & WRITE_PERM) == 0) { return -1; }
    Entry e = find_file(path.name, d.file.first_block, skip_flag ? SKIP_ALL : SKIP_NONE);
    if (e.file.name[0] == EOD_FLAG) { errno = ENOENT; return -1; }
    if (e.file.type == DIRECTORY_FILE) { errno = EISDIR; return -1; }
    if ((e.file.perm & WRITE_PERM) == 0) { errno = EACCES; return -1; }
    if (e.file.size == 0 && size > 0) {
        e.file.first_block = extend_data(0);
        if (e.file.first_block == 0) { return -1; }
    }
    if (offset + size > e.file.size) {
        e.file.size = offset + size;
    }
    time(&e.file.mtime);
    if (lseek(fs_fd, e.position, SEEK_SET) == -1) {
        cur_errno = ERR_PERM;
        p_perror("lseek");
        exit(EXIT_FAILURE);
    }
    if (write(fs_fd, &e.file, sizeof(File)) == -1) {
        cur_errno = ERR_PERM;
        p_perror("write");
        exit(EXIT_FAILURE);
    }
    e.position = seek_data(block_size * e.file.first_block, offset);
    if (e.position == -1) { return -1; }
    if (write_data(e.position, buf, size) == -1) { return -1; }
    return 0;
}

/**
 * @brief Set size of file at `path_str` to zero and free all of its allocated blocks.
 *
 * If the file cannot be located throws an `ENOENT` or `ENOTDIR` error.
 * If file is a directory and is non-empty throws an `ENOTEMPTY` error.
 * On any permissions error throws `EACCES`. File and directory need write permissions.
 * On success updates `size`, and `first_block` fields of file metadata accordingly.
 *
 * @return -1 on failure and 0 on success.
 * @param path_str Path to file to truncate.
 * @param skip_flag If the target file is a link, should we follow it?
 */
int truncate_file(char* path_str, bool skip_flag) {
    Path path = split_path(path_str);
    Entry d = find_directory(path.dir);
    if (d.file.name[0] == EOD_FLAG) { return -1; }
    if ((d.file.perm & WRITE_PERM) == 0) { return -1; }
    Entry e = find_file(path.name, d.file.first_block, skip_flag ? SKIP_ALL : SKIP_NONE);
    if (e.file.name[0] == EOD_FLAG) { errno = ENOENT; return -1; }
    if ((e.file.perm & WRITE_PERM) == 0) { errno = EACCES; return -1; }
    if (e.file.type == DIRECTORY_FILE) {
        if (e.file.size > 0) { errno = ENOTEMPTY; return -1; }
        return 0;
    }
    truncate_data(e.file.first_block);
    e.file.size = 0;
    e.file.first_block = LAST_BLOCK;
    if (lseek(fs_fd, e.position, SEEK_SET) == -1) {
        cur_errno = ERR_PERM;
        p_perror("lseek");
        exit(EXIT_FAILURE);
    }
    if (write(fs_fd, &e.file, sizeof(File)) == -1) {
        cur_errno = ERR_PERM;
        p_perror("write");
        exit(EXIT_FAILURE);
    }
    return 0;
}

/**
 * @brief Remove file at `path_str`'s directory entry.
 *
 * Sets `name[0]` to `REMOVED_FLAG` indicating that the file is deleted but may have its data still in use.
 * If the file cannot be located throws an `ENOENT` or `ENOTDIR` error.
 * On any permissions error throws `EACCES`. Directory needs write permissions.
 * The file's data is not cleaned up, use `truncate_file` for that.
 * On success, directory `size` containing file is reduced and `mtime` is updated.
 * Returns the position of the file on success for later cleanup.
 * TODO: what if file is deleted and overwritten? how will other processes using the file be able to see its data? 
 * need to not overwrite if entry is 2!!!
 *
 * @return -1 on failure and position of deleted file on success.
 * @param path_str Path to file to remove.
 */
int remove_file(char* path_str) {
    Path path = split_path(path_str);
    Entry d = find_directory(path.dir);
    if (d.file.name[0] == EOD_FLAG) { return -1; }
    if ((d.file.perm & WRITE_PERM) == 0) { return -1; }
    Entry e = find_file(path.name, d.file.first_block, SKIP_NONE);
    if (e.file.name[0] == EOD_FLAG) { errno = ENOENT; return -1; }
    d.file.size -= 64;
    time(&d.file.mtime);
    if (d.position >= 0) { // not root
        if (lseek(fs_fd, d.position, SEEK_SET) == -1) {
            cur_errno = ERR_PERM;
            p_perror("lseek");
            exit(EXIT_FAILURE);
        }
        if (write(fs_fd, &d.file, sizeof(File)) == -1) {
            cur_errno = ERR_PERM;
            p_perror("write");
            exit(EXIT_FAILURE);
        }
    }
    e.file.name[0] = REMOVED_FLAG;
    if (lseek(fs_fd, e.position, SEEK_SET) == -1) {
        cur_errno = ERR_PERM;
        p_perror("lseek");
        exit(EXIT_FAILURE);
    }
    if (write(fs_fd, &e.file, sizeof(File)) == -1) {
        cur_errno = ERR_PERM;
        p_perror("write");
        exit(EXIT_FAILURE);
    }
    return e.position;
}

/**
 * @brief Indicate that a deleted file's data has been cleaned up.
 *
 * Sets `name[0]` of the entry to `CLEANED_FLAG`.
 * TODO: implement the following sentence.
 * This allows the directory to reclaim the entry as the `first_block` pointer is no longer needed.
 * Note that working with raw positions is necessary because deleting a file
 * corrupts its name field and hence we can no longer access it with the other methods.
 *
 * @return -1 on failure and 0 on success.
 * @param position Position of directory entry to indicate has been cleaned up.
 */
int cleanup_file(int position) {
    int flag = CLEANED_FLAG;
    if (lseek(fs_fd, position, SEEK_SET) == -1) {
        cur_errno = ERR_PERM;
        p_perror("lseek");
        exit(EXIT_FAILURE);
    }
    if (write(fs_fd, &flag, 1) == -1) {
        cur_errno = ERR_PERM;
        p_perror("write");
        exit(EXIT_FAILURE);
    }
    return 0;
}

/**
 * @brief List the files in directory at `path_str`.
 *
 * Only lists non-deleted and non-EOD files.
 * Last file in list is an EOD file.
 * If the file cannot be located throws an `ENOENT` or `ENOTDIR` error.
 * If the file is not a directory throws an `ENOTDIR` error.
 * On any permissions error throws an `EACCES`. Directory needs read permissions.
 *
 * @return -1 on failure and 0 on success.
 * @param position Position of directory entry to indicate has been cleaned up.
 */
File* list_directory(char* path_str) {
    Path path = split_path(path_str);
    int count = 1;
    int i;
    Entry d = find_directory(path.dir);
    if (d.file.name[0] == EOD_FLAG) { return NULL; }
    Entry e;
    if (path.name == NULL) {
        e = d;
    } else {
        e = find_file(path.name, d.file.first_block, SKIP_ALL);
        if (e.file.name[0] == EOD_FLAG) { errno = ENOENT; return NULL; }
        if (e.file.type != DIRECTORY_FILE) { errno = ENOTDIR; return NULL; }
        if ((e.file.perm & READ_PERM) == 0) { errno = EACCES; return NULL; }
    }
    Entry* entries = enum_directory(e.file.first_block);
    for (i = 0; entries[i].file.name[0] != EOD_FLAG; ++i) {
        if (
            entries[i].file.name[0] != EOD_FLAG && 
            entries[i].file.name[0] != CLEANED_FLAG && 
            entries[i].file.name[0] != REMOVED_FLAG
        ) { count++; }
    }
    File* list = (File*) malloc(64 * count);
    count = 0;
    for (i = 0; entries[i].file.name[0] != EOD_FLAG; ++i) {
        if (
            entries[i].file.name[0] != EOD_FLAG && 
            entries[i].file.name[0] != CLEANED_FLAG && 
            entries[i].file.name[0] != REMOVED_FLAG
        ) { list[count] = entries[i].file; count++; }
    }
    list[count] = entries[i].file;
    return list;
}