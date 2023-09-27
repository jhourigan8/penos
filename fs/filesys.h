#include <stdint.h>
#include <stdbool.h>

/**
 * @file filesys.h
 * @brief An API for mounting and accessing filesystems.
 */

/**
 * @brief An unknown file type (e.g., an EOD entry.)
 */
#define UNKNOWN_FILE 0

/**
 * @brief A regular file type.
 */
#define REGULAR_FILE 1

/**
 * @brief A directory file type.
 */
#define DIRECTORY_FILE 2

/**
 * @brief A link file type. Can point to any other kind of file.
 */
#define LINK_FILE 4

/**
 * @brief Indicates permission to execute a file.
 *
 * Regular: no implemented behavior.
 * Directory: allows user to access files contained in directory (linux does this!)
 */
#define EXECUTE_PERM 1

/**
 * @brief Indicates permission to read a file.
 *
 * Regular: can read from file (call read_file).
 * Directory: can read contents of directory (call list_directory).
 */
#define READ_PERM 2

/**
 * @brief Indicates permission to write to a file.
 *
 * Regular: can write to file (call write_file).
 * Directory: can edit contents of directory (create / write / remove / truncate files in this directory).
 * Write and truncate are included because they change the size field which is part of the directory.
 */
#define WRITE_PERM 4

/**
 * @brief End of directory special marker in name[0].
 * 
 * Entries which have never been allocated contain only zeroes
 * and thus by default have this flag at name[0].
 * TODO: use this instead of 0 in pennfat.c
 */
#define EOD_FLAG 0x00

/**
 * @brief Cleaned up deleted file special marker in name[0].
 */
#define CLEANED_FLAG 0x01

/**
 * @brief Non-cleaned up deleted file special marker in name[0].
 */
#define REMOVED_FLAG 0x02

/**
 * @brief A file struct type as specified in the PennOS writeup.
 */
typedef struct file {
    /**
    * @brief File name string (null terminated). See macros above for special marker meanings.
    *
    * name[0] is also a special marker:
    * 0 => end of directory.
    * 1 => deleted entry, file also deleted.
    * 2 => deleted entry, file still exists.
    */
    char name[32];
    /**
    * @brief Number of bytes in the file.
    *
    * If the file is a directory is equal to (File struct size) * (Number of non-deleted files)
    */
    uint32_t size;
    /**
    * @brief First block number of the file.
    *
    * Equal to LAST_BLOCK if size is zero.
    */
    uint16_t first_block;
    /**
    * @brief File type indicator. See macros above.
    */
    uint8_t type;
    /**
    * @brief File permissions mode. See macros above.
    */
    uint8_t perm;
    /**
    * @brief Last creation/modification time as returned by time(2) in Linux.
    *
    * TODO: decide exactly when time should be updated.
    */
    time_t mtime;
} File;

// Documentation in filesys.c

int init_fs(char* fs, int new_fat_blocks, int new_block_size_config);

int mount_fs(char* fs);

int unmount_fs();

int create_file(char* path_str, uint8_t type);

int set_file(char* path_str, File f, bool skip_flag);

File get_file(char* path_str, bool skip_flag);

int read_file(char* path_str, int offset, uint8_t* buf, int size);

int write_file(char* path_str, int offset, uint8_t* buf, int size, bool skip_flag);

int truncate_file(char* path_str, bool skip_flag);

int remove_file(char* path_str);

int cleanup_file(int offset);

int seek_data(int position, int offset);

File* list_directory(char* path_str);