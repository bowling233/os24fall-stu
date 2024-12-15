#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "stdint.h"
#include "proc.h"

#define SYS_OPENAT  56
#define SYS_CLOSE   57
#define SYS_LSEEK   62
#define SYS_READ    63
#define SYS_WRITE   64
#define SYS_GETPID  172
#define SYS_CLONE   220

extern struct task_struct *current;

void do_syscall(struct pt_regs *regs);

#endif
