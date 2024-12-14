#ifndef __PROC_H__
#define __PROC_H__

#include "stdint.h"
#include "vm.h"

#define NR_TASKS (1 + 8)

#define FIRST_TASK task[0]
#define LAST_TASK task[NR_TASKS - 1]

#define TASK_RUNNING 0 // 为了简化实验，所有的线程都只有一种状态

#define PRIORITY_MIN 1
#define PRIORITY_MAX 10

extern char _stext[], _etext[], _srodata[], _erodata[], _sdata[], _edata[], _sbss[], _ebss[];

/* 线程状态段数据结构 */
struct thread_struct
{
    uint64_t ra;
    uint64_t sp;
    uint64_t s[12];
    uint64_t sepc, sstatus, sscratch;
};

/* 线程数据结构 */
struct task_struct
{
    uint64_t state;    // 线程状态
    uint64_t counter;  // 运行剩余时间
    uint64_t priority; // 运行优先级 1 最低 10 最高
    uint64_t pid;      // 线程 id

    struct thread_struct thread;
    uint64_t *pgd;
    struct mm_struct mm;
    struct files_struct *files;
};

struct pt_regs
{
    uint64_t x[31]; // x1-x31
    uint64_t sepc;
    // where is sstatus?
};
extern char __ret_from_fork[];

/* 线程初始化，创建 NR_TASKS 个线程 */
void task_init();

/* 在时钟中断处理中被调用，用于判断是否需要进行调度 */
void do_timer();

/* 调度程序，选择出下一个运行的线程 */
void schedule();

/* 线程切换入口函数 */
void switch_to(struct task_struct *next);

/* dummy funciton: 一个循环程序，循环输出自己的 pid 以及一个自增的局部变量 */
void dummy();

extern void __dummy(); // entry.S
extern uint64_t swapper_pg_dir[];
extern char _sramdisk[], _eramdisk[];
extern void __switch_to(struct task_struct *prev, struct task_struct *next);

// in proc.c
extern struct task_struct *idle;    // idle process
extern struct task_struct *current; // 指向当前运行线程的 task_struct
extern struct task_struct *task[];  // 线程数组，所有的线程都保存在此
extern uint64_t nr_tasks;

#endif
