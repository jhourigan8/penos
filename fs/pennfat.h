/**
 * @file pennfat.h
 * @brief Filesystem related shell commands.
 */

// Documentation in pennfat.c

void pf_mkfs(int argc, char** args);

void pf_mount(int argc, char** args);

void pf_umount(int argc, char** args);

void pf_touch(int argc, char** args);

void pf_rm(int argc, char** args);

void pf_mv(int argc, char** args);

void pf_cp(int argc, char** args);

void pf_cat(int argc, char** args);

void pf_ls(int argc, char** args);

void pf_chmod(int argc, char** args);

void pf_cd(int argc, char** args);

void pf_mkdir(int argc, char** args);

void pf_rmdir(int argc, char** args);

void pf_pwd(int argc, char** args);

void pf_ln(int argc, char** args);