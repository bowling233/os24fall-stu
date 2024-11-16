#include "syscall.h"

uint64_t sys_write(int fd, void *buf, int count)
{
    if (fd == 1 || fd == 2)
    {
        for (int i = 0; i < count; ++i)
        {
            printk("%c", ((char *)buf)[i]);
        }
        return count;
    }
    return -1;
}

uint64_t sys_getpid()
{
    return current->pid;
}