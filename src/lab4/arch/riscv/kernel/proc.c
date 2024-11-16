#include "mm.h"
#include "defs.h"
#include "proc.h"
#include "stdlib.h"
#include "printk.h"
#include "string.h"
#include "vm.h"

#define print_task(action, task) \
    printk(action " [PID = %d PRIORITY = %d COUNTER = %d]\n", \
        task->pid, task->priority, task->counter);

extern void __dummy();
extern uint64_t swapper_pg_dir[];
extern uint64_t _sramdisk[], _eramdisk[];

struct task_struct *idle;           // idle process
struct task_struct *current;        // 指向当前运行线程的 task_struct
struct task_struct *task[NR_TASKS]; // 线程数组，所有的线程都保存在此

extern void __switch_to(struct task_struct *prev, struct task_struct *next);

void switch_to(struct task_struct *next)
{
    //Log("switch_to");
    if (current == next)
    {
        return;
    }
    struct task_struct *prev = current;
    current = next;
    print_task("SWITCH TO", next);
    __switch_to(prev, next);
}

void do_timer()
{
    //Log("do_timer");
    // 1. 如果当前线程是 idle 线程或当前线程时间片耗尽则直接进行调度
    if (!(current == idle || current->counter == 0))
    {
        // 2. 否则对当前线程的运行剩余时间减 1，若剩余时间仍然大于 0 则直接返回，否则进行调度
        current->counter--;
        if (current->counter > 0)
        {
            return;
        }
    }
    schedule();
}

void schedule()
{
#ifdef DEBUG
    Log("schedule");
#endif
    uint64_t i, next, c;
    struct task_struct **p;

    while (1)
    {
        c = 0;
        next = 0;
        i = NR_TASKS;
        p = &task[NR_TASKS];
        while (--i)
        {
            if (!*--p)
                continue;
            // find the task with the highest priority
            if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
                c = (*p)->counter, next = i;
        }
        if (c)
            break;
        // all tasks have run out of time, set to priority
        for (p = &LAST_TASK ; p > &FIRST_TASK ; --p)
            if (*p){
                (*p)->counter = ((*p)->counter >> 1) +
                                (*p)->priority;
                print_task("SET ", (*p));
            }
    }
    switch_to(task[next]);
}

void task_init()
{
#ifdef DEBUG
    Log("task_init");
#endif
    srand(2024);

    // 1. 调用 kalloc() 为 idle 分配一个物理页
    idle = (struct task_struct *)kalloc();
    // 2. 设置 state 为 TASK_RUNNING;
    idle->state = TASK_RUNNING;
    // 3. 由于 idle 不参与调度，可以将其 counter / priority 设置为 0
    idle->counter = 0;
    idle->priority = 0;
    // 4. 设置 idle 的 pid 为 0
    idle->pid = 0;
    // 5. 将 current 和 task[0] 指向 idle
    current = idle;
    task[0] = idle;
#ifdef DEBUG
    print_task("SET", idle);
#endif

    // 1. 参考 idle 的设置，为 task[1] ~ task[NR_TASKS - 1] 进行初始化
    for (int i = 1; i < NR_TASKS; i++)
    {
        task[i] = (struct task_struct *)kalloc();
        // 2. 其中每个线程的 state 为 TASK_RUNNING, 此外，counter 和 priority 进行如下赋值：
        task[i]->state = TASK_RUNNING;
        //     - counter  = 0;
        task[i]->counter = 0;
        //     - priority = rand() 产生的随机数（控制范围在 [PRIORITY_MIN, PRIORITY_MAX] 之间）
        task[i]->priority = PRIORITY_MIN + rand() % (PRIORITY_MAX - PRIORITY_MIN + 1);
        task[i]->pid = i;
        // 3. 为 task[1] ~ task[NR_TASKS - 1] 设置 thread_struct 中的 ra 和 sp
        //     - ra 设置为 __dummy（见 4.2.2）的地址
        task[i]->thread.ra = (uint64_t)__dummy;
        //     - sp 设置为该线程申请的物理页的高地址
        task[i]->thread.sp = (uint64_t)task[i] + PGSIZE;
        // 将 sepc 设置为 USER_START
        task[i]->thread.sepc = USER_START;
        // 配置 sstatus 中的 SPP（使得 sret 返回至 U-Mode）、SPIE（sret 之后开启中断）、SUM（S-Mode 可以访问 User 页面）
        task[i]->thread.sstatus = ~SPP | SPIE | SUM;
        // 将 sscratch 设置为 U-Mode 的 sp，其值为 USER_END（将用户态栈放置在 user space 的最后一个页面）
        task[i]->thread.sscratch = USER_END;
        // 为了避免 U-Mode 和 S-Mode 切换的时候切换页表，我们将内核页表 swapper_pg_dir 复制到每个进程的页表中
        task[i]->pgd = (uint64_t *)alloc_page();
        memcpy(task[i]->pgd, swapper_pg_dir, PGSIZE);
        // 二进制文件需要先被拷贝到一块新的、供某个进程专用的内存之后再进行映射，来防止所有的进程共享数据，造成预期外的进程间相互影响。
        // debug
        printk("sramdisk: %p, eramdisk: %p\n", _sramdisk, _eramdisk);
        printk("%d\n", (_eramdisk - _sramdisk) / PGSIZE + 1);
        uint64_t *binary = (uint64_t *)alloc_pages((_eramdisk - _sramdisk) / PGSIZE + 1);
        memcpy(binary, (void *)_sramdisk, _eramdisk - _sramdisk);
        // 将 uapp 所在的页面映射到每个进行的页表中
        create_mapping(task[i]->pgd, USER_START, (uint64_t)binary, _eramdisk - _sramdisk, PTE_R | PTE_W | PTE_X | PTE_V);
        // 用户态栈：我们可以申请一个空的页面来作为用户态栈，并映射到进程的页表中
        uint64_t *user_stack = (uint64_t *)alloc_page();
        create_mapping(task[i]->pgd, USER_END - PGSIZE, (uint64_t)user_stack, PGSIZE, PTE_R | PTE_W | PTE_V);
#ifdef DEBUG
        print_task("SET", task[i]);
#endif
    }

    /* YOUR CODE HERE */

    printk("...task_init done!\n");
}

#if TEST_SCHED
#define MAX_OUTPUT ((NR_TASKS - 1) * 10)
char tasks_output[MAX_OUTPUT];
int tasks_output_index = 0;
char expected_output[] = "2222222222111111133334222222222211111113";
#include "sbi.h"
#endif

void dummy()
{
    uint64_t MOD = 1000000007;
    uint64_t auto_inc_local_var = 0;
    int last_counter = -1;
    while (1)
    {
        if ((last_counter == -1 || current->counter != last_counter) && current->counter > 0)
        {
            if (current->counter == 1)
            {
                --(current->counter); // forced the counter to be zero if this thread is going to be scheduled
            } // in case that the new counter is also 1, leading the information not printed.
            last_counter = current->counter;
            auto_inc_local_var = (auto_inc_local_var + 1) % MOD;
            printk("[PID = %d] is running. auto_inc_local_var = %d\n", current->pid, auto_inc_local_var);
            #if TEST_SCHED
            tasks_output[tasks_output_index++] = current->pid + '0';
            if (tasks_output_index == MAX_OUTPUT)
            {
                for (int i = 0; i < MAX_OUTPUT; ++i)
                {
                    if (tasks_output[i] != expected_output[i])
                    {
                        printk("\033[31mTest failed!\033[0m\n");
                        printk("\033[31m    Expected: %s\033[0m\n", expected_output);
                        printk("\033[31m    Got:      %s\033[0m\n", tasks_output);
                        sbi_system_reset(SBI_SRST_RESET_TYPE_SHUTDOWN, SBI_SRST_RESET_REASON_NONE);
                    }
                }
                printk("\033[32mTest passed!\033[0m\n");
                printk("\033[32m    Output: %s\033[0m\n", expected_output);
                sbi_system_reset(SBI_SRST_RESET_TYPE_SHUTDOWN, SBI_SRST_RESET_REASON_NONE);
            }
#endif
        }
    }
}
