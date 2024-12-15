#include "fs.h"
#include "vfs.h"
#include "mm.h"
#include "string.h"
#include "printk.h"
#include "fat32.h"

struct files_struct *file_init()
{
    // alloc pages for files_struct, and initialize stdin, stdout, stderr
    // 这个函数需要大家在 proc.c 中的 task_init 函数中为每个进程调用，创建文件表并保存在 task struct 中。
    // 根据 files_struct 的大小分配页空间
    struct files_struct *ret = (struct files_struct *)alloc_page();
    // 保证其他未使用的文件的 opened 字段为 0
    for(int i = 0; i < MAX_FILE_NUMBER; i++)
    {
    }
    // 为 stdin、stdout、stderr 赋值，比如 stdin 你可以：
    ret->fd_array[0].opened = 1;
    ret->fd_array[0].perms = FILE_READABLE;
    ret->fd_array[0].read = stdin_read;
    ret->fd_array[1].opened = 1;
    ret->fd_array[1].perms = FILE_WRITABLE;
    ret->fd_array[1].write = stdout_write;
    ret->fd_array[2].opened = 1;
    ret->fd_array[2].perms = FILE_WRITABLE;
    ret->fd_array[2].write = stderr_write;
    // 这里的 read / write 函数可以留到等下来实现
    return ret;
}

uint32_t get_fs_type(const char *filename)
{
    uint32_t ret;
    if (memcmp(filename, "/fat32/", 7) == 0)
    {
        ret = FS_TYPE_FAT32;
    }
    else if (memcmp(filename, "/ext2/", 6) == 0)
    {
        ret = FS_TYPE_EXT2;
    }
    else
    {
        ret = -1;
    }
    return ret;
}

int32_t file_open(struct file *file, const char *path, int flags)
{
    Log("%s", path);
    file->opened = 1;
    file->perms = flags;
    file->cfo = 0;
    file->fs_type = get_fs_type(path);
    memcpy(file->path, path, strlen(path) + 1);

    if (file->fs_type == FS_TYPE_FAT32)
    {
        file->lseek = fat32_lseek;
        file->write = fat32_write;
        file->read = fat32_read;
        file->fat32_file = fat32_open_file(path);
        // check if fat32_file is valid (i.e. successfully opened) and return
        if (file->fat32_file.cluster == 0)
        {
            return -1;
        }
        Log("%s opened", path);
        return 0;
    }
    else if (file->fs_type == FS_TYPE_EXT2)
    {
        printk(RED "Unsupport ext2\n" CLEAR);
        return -1;
    }
    else
    {
        printk(RED "Unknown fs type: %s\n" CLEAR, path);
        return -1;
    }
}