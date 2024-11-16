#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "stdint.h"

#define __NR_write 64
#define __NR_getpid 172

extern struct task_struct *current;

uint64_t sys_write(int fd, void *buf, int count);
uint64_t sys_getpid();

#endif
