#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include "disk.h"
#include "stdio.h"
#include "myfs.h"

typedef struct super_block super_block;
typedef struct dir_entry dir_entry;
typedef struct file_descriptor file_descriptor;
typedef struct fat fat;

//meta data
struct super_block
{
    int version;
    int fat_index;
    int fat_len;
    int dir_index;
    int dir_len;
    int data_index;
};

struct dir_entry
{
    int used;
    char file_name[FILE_NAME_LEN];
    int fat_index;
    int ref_cnt;
};

struct fat
{
    int next_block;//default -1
    int used;//used:1 unused:0
    int offset;//how many bytes in this block
};

struct file_descriptor
{
    int used;
    int file_head;
    int offset;
};

/*function declarations*/
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

//global variables
super_block *super_blocks;
file_descriptor *file_descriptors;
dir_entry *dir_entries;
fat *fats;
int free_disks;

//helper functions
int write_metadata()
{
    int i;
    //write super block
    char* buf = malloc(BLOCK_SIZE);
    memcpy(buf, super_blocks, sizeof(super_block));
    block_write(0, buf);
    free(buf);
    //write fat
    char* fat_write = malloc(DATA_BLOCKS * sizeof(fat));
    //use fat_free to free
    char* fat_free = fat_write;
    memcpy(fat_write, fats, DATA_BLOCKS * sizeof(fat));
    for (i = super_blocks->fat_index; i<(super_blocks->fat_index + sizeof(fat)); i++)
    {
        buf = malloc(BLOCK_SIZE);
        memcpy(buf, fat_write, BLOCK_SIZE);
        block_write(i, buf);
        free(buf);
        fat_write += BLOCK_SIZE;
    }
    free(fat_free);
    //write directory
    buf = malloc(DIR_ENRIES * sizeof(dir_entry));
    memcpy(buf, dir_entries, DIR_ENRIES * sizeof(dir_entry));
    block_write(super_blocks->dir_index, buf);
    free(buf);
    return 0;
}

int get_free_descriptor()
{
    int i;
    for (i = 0; i<FILE_DESCRIPTORS; i++)
    {
        if (file_descriptors[i].used == 0)
        {
            file_descriptors[i].used = 1;
            return i;
        }
    }
    return -1;
}

int get_free_fat()
{
    int i;
    for (i = 0; i<DATA_BLOCKS; i++)
    {
        if (fats[i].used == 0)
        {
            free_disks--;
            return i;
        }
    }
    return -1;
}

int load_file(int fat_index, char* buf)
{
    if(buf==NULL)
    {
        return -1;
    }
    int block_count = 0;
    char* copy_buf = buf;
    while (fats[fat_index].used == 1)
    {
        block_count++;
        char* temp_buf = malloc(BLOCK_SIZE);
        block_read(fat_index + DATA_BIAS, temp_buf);
        if (fats[fat_index].next_block<0)
        {
            memcpy(copy_buf, temp_buf, (size_t) fats[fat_index].offset);
            free(temp_buf);
            //return filesize
            return fats[fat_index].offset + (block_count - 1)*BLOCK_SIZE;
        }
        fat_index = fats[fat_index].next_block;
        memcpy(copy_buf, temp_buf, BLOCK_SIZE);
        free(temp_buf);
        copy_buf += BLOCK_SIZE;
    }
    return -1;
}

int get_fat_blocks(int index)
{
    int block_count = 0;
    while (index >= 0 && fats[index].used == 1)
    {
        block_count++;
        index = fats[index].next_block;
    }
    return block_count;
}

//reset blocks in fat,if number of blocks increased then return the increased number else return reduced number
int reset_fat(int fat_index, int blocks)
{
    int block_changed = 0;
    int index = fat_index;
    if (index<0 || blocks <= 0)
    {
        return -1;
    }
    int block_count = get_fat_blocks(index);
    index = fat_index;
    int last_index = fat_index;
    if (block_count<blocks)
    {
        int append_blocks = blocks - block_count;
        while (index >= 0 && fats[index].used == 1)
        {
            last_index = index;
            index = fats[index].next_block;
        }
        index = last_index;
        while (append_blocks>0)
        {
            append_blocks--;
            int free_block = get_free_fat();
            if (free_block<0)
            {
                break;
            }
            fats[index].next_block = free_block;
            index = free_block;
            fats[index].used = 1;
            block_changed++;
        }
        return block_changed;
    }
    else if (block_count>blocks)
    {
        int i;
        for (i = 0; i<blocks; i++)
        {
            index = fats[index].next_block;
        }
        int temp;
        while (i<block_count)
        {
            temp = index;
            index = fats[temp].next_block;
            fats[temp].next_block = -1;
            fats[temp].offset = 0;
            fats[temp].used = 0;
            free_disks++;
            i++;
        }
        return blocks-block_count;
    }
    return 0;
}

//file system management
int make_fs(char *disk_name)
{
    //init disk
    if (make_disk(disk_name) < 0)
    {
        return -1;
    }
    if (open_disk(disk_name) < 0)
    {
        return -1;
    }
    //init super block
    super_blocks = (super_block*)(malloc(sizeof(super_block)));
    super_blocks->version = 1;
    super_blocks->fat_index = 1;
    super_blocks->fat_len = 12;
    super_blocks->dir_index = 13;
    super_blocks->dir_len = 1;
    super_blocks->data_index = DATA_BLOCKS;
    //init fat
    fats = (fat*)malloc(DATA_BLOCKS * sizeof(fat));
    int i;
    for (i = 0; i<DATA_BLOCKS; i++)
    {
        fats[i].used = 0;
        fats[i].next_block = -1;
        fats[i].offset = 0;
    }
    //init directory
    dir_entries = (dir_entry*)malloc(DIR_ENRIES * sizeof(dir_entry));
    for (i = 0; i<DIR_ENRIES; i++)
    {
        dir_entries[i].used = 0;
        dir_entries[i].fat_index = -1;
        dir_entries[i].ref_cnt = 0;
    }
    write_metadata();
    close_disk();
    return 0;
}

int mount_fs(char* disk_name)
{
    if (open_disk(disk_name) < 0)
    {
        return -1;
    }
    //init metadata structures
    free(super_blocks);
    free(fats);
    free(dir_entries);
    super_blocks = (super_block*)(malloc(sizeof(super_block)));
    fats = (fat*)malloc(DATA_BLOCKS * sizeof(fat));
    dir_entries = (dir_entry*)malloc(DIR_ENRIES * sizeof(dir_entry));

    //load super block
    char* buf_sb = malloc(BLOCK_SIZE);
    block_read(0, buf_sb);
    memcpy(super_blocks, buf_sb, sizeof(super_block));
    free(buf_sb);
    //check for a valid file system
    if (super_blocks->version != 1)
    {
        return -1;
    }
    //load fat
    char* fat_r = (char*)malloc(DATA_BLOCKS * sizeof(fat));
    char* fat_f = fat_r;
    int i;
    char* buf = malloc(BLOCK_SIZE);
    for (i = super_blocks->fat_index; i<(super_blocks->fat_index + super_blocks->fat_len); i++)
    {
        block_read(i, buf);
        memcpy(fat_r, buf, BLOCK_SIZE);
        fat_r += BLOCK_SIZE;
    }
    free(buf);
    memcpy(fats, fat_f, DATA_BLOCKS * sizeof(fat));
    //calculate free blocks
    free_disks = 0;
    for(i=0;i<DATA_BLOCKS;i++)
    {
        if(fats[i].used==0)
        {
            free_disks++;
        }
    }
    free(fat_f);
    //load directory
    //buf = malloc(DIR_ENRIES*sizeof(dir_entry));
    buf = malloc(BLOCK_SIZE);
    block_read(super_blocks->dir_index, buf);
    memcpy(dir_entries, buf, DIR_ENRIES * sizeof(dir_entry));
    free(buf);
    //init descriptors
    file_descriptors = (file_descriptor*)malloc(FILE_DESCRIPTORS * sizeof(file_descriptor));
    for (i = 0; i<FILE_DESCRIPTORS; i++)
    {
        file_descriptors[i].file_head = -1;
        file_descriptors[i].offset = 0;
        file_descriptors[i].used = 0;
    }
    return 0;
}

int umount_fs(char* disk_name)
{
    int i;
    if(dir_entries!=NULL)
    {
        for (i = 0; i<DIR_ENRIES; i++)
        {
            dir_entries[i].ref_cnt = 0;
        }
        if(fats!=NULL&&super_blocks!=NULL)
        {
            write_metadata();
        }
    }
    free(fats);
    free(super_blocks);
    free(dir_entries);
    free(file_descriptors);
    fats = NULL;
    super_blocks = NULL;
    dir_entries = NULL;
    file_descriptors = NULL;
    if (close_disk() < 0)
    {
        return -1;
    }
    return 0;
}

//file system operations
int fs_create(char* name)
{
    if (name == NULL || strlen(name)>15)
    {
        return -1;
    }
    int i;
    //check for already created file
    for (i=0;i<DIR_ENRIES;i++)
    {
        if(dir_entries[i].used==1)
        {
            if(strcmp(name,dir_entries[i].file_name)==0)
            {
                return -1;
            }
        }
    }
    for (i = 0; i<DIR_ENRIES; i++)
    {
        if (dir_entries[i].used == 0)
        {
            int j = 0;
            for (j = 0; j<strlen(name); j++)
            {
                dir_entries[i].file_name[j] = name[j];
            }
            //add end of str
            dir_entries[i].file_name[j] = '\0';
            int head = get_free_fat();
            if (head<0)
            {
                return -1;
            }
            //set fat info
            fats[head].used = 1;
            dir_entries[i].fat_index = head;
            dir_entries[i].used = 1;
            dir_entries[i].ref_cnt = 0;
            break;
        }
    }
    if (i == DIR_ENRIES)
    {
        return -1;
    }
    return 0;
}

int fs_open(char* name)
{
    int i;
    for (i = 0; i<DIR_ENRIES; i++)
    {
        if (dir_entries[i].used==1&&strcmp(name, dir_entries[i].file_name) == 0)
        {
            int descriptor_id = get_free_descriptor();
            if (descriptor_id<0)
            {
                return -1;
            }
            dir_entries[i].ref_cnt++;
            file_descriptors[descriptor_id].used = 1;
            file_descriptors[descriptor_id].file_head = i;
            file_descriptors[descriptor_id].offset = 0;
            return descriptor_id;
        }
    }
    return -1;
}

int fs_close(int fildes)
{
    if (fildes<0 || fildes >= FILE_DESCRIPTORS || file_descriptors[fildes].used <= 0)
    {
        return -1;
    }
    file_descriptors[fildes].used = 0;
    file_descriptors[fildes].offset = 0;
    dir_entries[file_descriptors[fildes].file_head].ref_cnt--;
    file_descriptors[fildes].file_head = -1;
    return 0;
}

int fs_delete(char* name)
{
    int i, temp,j;
    for (i = 0; i<DIR_ENRIES; i++)
    {
        if (dir_entries[i].used==1&&strcmp(name, dir_entries[i].file_name) == 0)
        {
            if (dir_entries[i].ref_cnt>0)
            {
                return -1;
            }
            int fat_index = dir_entries[i].fat_index;
            while (fat_index >= 0 && fats[fat_index].used != 0)
            {
                fats[fat_index].used = 0;
                fats[fat_index].offset = 0;
                temp = fat_index;
                fat_index = fats[fat_index].next_block;
                fats[temp].next_block = -1;
            }
            for(j=0;j<FILE_NAME_LEN;j++)
            {
                dir_entries[i].file_name[j] = (char)0;
            }
            dir_entries[i].used = 0;
            dir_entries[i].fat_index = -1;
            return 0;
        }
    }
    return -1;
}

int fs_get_filesize(int fildes)
{
    if (fildes<0 || fildes >= FILE_DESCRIPTORS)
    {
        return -1;
    }
    if (file_descriptors[fildes].used<=0)
    {
        return -1;
    }
    int fat_index = dir_entries[file_descriptors[fildes].file_head].fat_index;
    if (fat_index<0)
    {
        return -1;
    }
    int block_count = 0;
    while (fats[fat_index].used == 1)
    {
        block_count++;
        if (fats[fat_index].next_block <= 0)
        {
            return fats[fat_index].offset + (block_count - 1)*BLOCK_SIZE;
        }
        fat_index = fats[fat_index].next_block;
    }
    return -1;
}

int fs_write(int fildes, void* buf, size_t nbyte)
{
    if (fildes<0 || fildes >= FILE_DESCRIPTORS || file_descriptors[fildes].used == 0 || nbyte <= 0)
    {
        return -1;
    }
    int fat_index = dir_entries[file_descriptors[fildes].file_head].fat_index;
    char* temp_buf = NULL;
    int file_size = fs_get_filesize(fat_index);
    if(file_size>0)
    {
        temp_buf = (char*)malloc((size_t) file_size);
        if (load_file(fat_index, temp_buf)<0)
        {
            return -1;
        }
    }
    int new_file_size = (int)(file_descriptors[fildes].offset + nbyte);
    new_file_size = new_file_size>file_size ? new_file_size : file_size;
    char* newfile = malloc((size_t)new_file_size);
    char* newfile_free = newfile;
    int block_count = (new_file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int prev_file_blocks = get_fat_blocks(fat_index);
    int reset_result = reset_fat(fat_index, block_count);
    if(file_size>0)
    {
        memcpy(newfile, temp_buf, (size_t) file_size);
    }
    memcpy(newfile + file_descriptors[fildes].offset, buf, nbyte);
    int write_size = (int) nbyte;
    if(reset_result>0)
    {
        //in case the disk is full
        int cutted_blocks = block_count - prev_file_blocks - reset_result;
        if(cutted_blocks>0)
        {
            write_size -= cutted_blocks*BLOCK_SIZE;
            new_file_size -= cutted_blocks*BLOCK_SIZE;
        }
    }
    while (new_file_size>BLOCK_SIZE)
    {
        fats[fat_index].offset = BLOCK_SIZE;
        block_write(fat_index + DATA_BIAS, newfile);
        newfile += BLOCK_SIZE;
        new_file_size -= BLOCK_SIZE;
        fat_index = fats[fat_index].next_block;
    }
    //write last block
    fats[fat_index].offset = new_file_size;
    block_write(fat_index + DATA_BIAS, newfile);
    file_descriptors[fildes].offset += write_size;
    free(temp_buf);
    free(newfile_free);
    return write_size;
}

int fs_read(int fildes, void* buf, size_t nbyte)
{
    if (fildes<0 || fildes >= FILE_DESCRIPTORS) {
        return -1;
    }
    int fat_index = dir_entries[file_descriptors[fildes].file_head].fat_index;
    if (fat_index<0)
    {
        return -1;
    }
    if (nbyte<0)
    {
        return -1;
    }
    char* temp_buf;
    int file_size = fs_get_filesize(fat_index);
    temp_buf = malloc((size_t) file_size);
    if (load_file(fat_index, temp_buf)<0)
    {
        return -1;
    }
    int max_read = file_size - file_descriptors[fildes].offset;
    int actual_read = (int)(nbyte > max_read ? (size_t) max_read : nbyte);
    memcpy(buf, temp_buf + file_descriptors[fildes].offset, (size_t) actual_read);
    free(temp_buf);
    file_descriptors[fildes].offset += actual_read;
    return actual_read;
}

int fs_lseek(int fildes, off_t offset)
{
    if (fildes<0 || fildes >= FILE_DESCRIPTORS||offset<0||file_descriptors[fildes].used<=0) {
        return -1;
    }
    int fat_index = dir_entries[file_descriptors[fildes].file_head].fat_index;
    if (fat_index<0)
    {
        return -1;
    }
    int file_size = fs_get_filesize(fildes);
    if (offset>file_size)
    {
        return -1;
    }
    file_descriptors[fildes].offset = (int)offset;
    return 0;
}

int fs_truncate(int fildes, off_t length)
{
    if (fildes<0 || fildes >= FILE_DESCRIPTORS) {
        return -1;
    }
    int fat_index = dir_entries[file_descriptors[fildes].file_head].fat_index;
    if (fat_index<0)
    {
        return -1;
    }
    int file_size = fs_get_filesize(fildes);
    if (length>file_size)
    {
        return -1;
    }
    char* buf = malloc((size_t) length);
    char* temp_buf = malloc((size_t) file_size);
    if (load_file(fat_index, temp_buf)<0)
    {
        return -1;
    }
    memcpy(buf, temp_buf, (size_t) length);
    reset_fat(fat_index, (int) ((length + 1 - BLOCK_SIZE) / BLOCK_SIZE));

    while (length>BLOCK_SIZE)
    {
        fats[fat_index].offset = BLOCK_SIZE;
        block_write(fat_index + DATA_BIAS, buf);
        buf += BLOCK_SIZE;
        length -= BLOCK_SIZE;
        fat_index = fats[fat_index].next_block;
    }
    fats[fat_index].offset = (int) length;
    block_write(fat_index + DATA_BIAS, buf);
    free(buf);
    free(temp_buf);
    return 0;
}
