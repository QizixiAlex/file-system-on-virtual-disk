//
// Created by alex on 11/19/16.
//

#ifndef CSE_LAB1_FS_H
#define CSE_LAB1_FS_H

#include <stdio.h>

#define SIZE 4000
#define FILE_NAME_LEN 16
#define DATA_BLOCKS 4096
#define DIR_ENRIES 64
#define FILE_DESCRIPTORS 32
#define DATA_BIAS 4096

int make_fs(char *disk_name);
int mount_fs(char *disk_name);
int umount_fs(char *disk_name);
int fs_open(char *name);
int fs_close(int fildes);
int fs_create(char *name);
int fs_delete(char *name);
int fs_read(int fildes,void *buf,size_t nbyte);
int fs_write(int fildes,void *buf,size_t nbyte);
int fs_get_filesize(int fildes);
int fs_lseek(int fildes,off_t offset);
int fs_truncate(int fildes,off_t length);

#endif //CSE_LAB1_FS_H
