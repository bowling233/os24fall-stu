#include "syscall.h"
#include "printk.h"
#include "stdint.h"
#include "fs.h"
#include "proc.h"
#include "mm.h"
#include "string.h"

uint64_t do_fork(struct pt_regs *regs)
{
    uint64_t new_pid = ++nr_tasks;
    printk("[PID = %d] forked from [PID = %d]", new_pid, current->pid);
    // 将新进程加入调度队列
    struct task_struct *new_task = task[new_pid] = (struct task_struct *)alloc_page();
    memcpy(new_task, current, PGSIZE);
    new_task->pid = new_pid;
    new_task->thread.ra = (uint64_t)__ret_from_fork;
#ifdef DEBUG
    Log("old_task page = %lx", current);
    Log("old_task sp = %lx, offset in page = %lx", regs->x[1], regs->x[1] - (uint64_t)current);
    Log("new_task page = %lx", new_task);
#endif
    uint64_t regs_offest = (uint64_t)regs - (uint64_t)current;
    new_task->thread.sp = ((uint64_t)new_task + regs_offest);
    struct pt_regs *new_regs = (struct pt_regs *)new_task->thread.sp;
    new_regs->x[9] = 0;
    new_regs->sepc += 4;
    new_task->thread.sscratch = csr_read(sscratch);
    new_task->thread.sstatus = current->thread.sstatus;
    new_task->mm.mmap = NULL;
    // 拷贝内核页表 swapper_pg_dir
    new_task->pgd = sv39_pg_dir_dup(swapper_pg_dir);
    // 遍历父进程 vma，并遍历父进程页表
    struct vm_area_struct *parent_vma = current->mm.mmap;
    while (parent_vma)
    {
#ifdef DEBUG
        Log("parent_vma: %lx %lx %lx %lx %lx", parent_vma->vm_start, parent_vma->vm_end, parent_vma->vm_pgoff, parent_vma->vm_filesz, parent_vma->vm_flags);
#endif
        // 将这个 vma 也添加到新进程的 vma 链表中
        do_mmap(&new_task->mm, parent_vma->vm_start, parent_vma->vm_end - parent_vma->vm_start, parent_vma->vm_pgoff, parent_vma->vm_filesz, parent_vma->vm_flags);
        for (uint64_t parent_page = parent_vma->vm_start; parent_page < parent_vma->vm_end; parent_page += PGSIZE)
        {
#ifdef DEBUG
            Log("COW parent_page: %lx", parent_page);
#endif
            uint64_t *pte_p = find_pte(current->pgd, parent_page);
            uint64_t pte = pte_p ? *pte_p : 0;
            if (!pte)
                continue;

            // 将物理页的引用计数加一
            uint64_t *parent_page_va = (uint64_t *)PTE2VA(pte);
            get_page((void *)parent_page_va);
            // 将父进程的该地址对应的页表项的 PTE_W 位置 0
            *pte_p &= ~PTE_W;
            pte = *pte_p; // 重新读取 pte
            // 为子进程创建一个新的页表项，指向父进程的物理页，且权限不带 PTE_W
            create_mapping(new_task->pgd, parent_page, VA2PA(PTE2VA(pte)), PGSIZE, pte & PTE_FLAGS_MASK);
        }
        // flush TLB because we modifid page table in use
        asm volatile("sfence.vma");
        parent_vma = parent_vma->vm_next;
    }
    // 处理父子进程的返回值
    // 父进程通过 do_fork 函数直接返回子进程的 pid，并回到自身运行
    // ：这里通过在 trap_handler 中设置返回值实现
    // 子进程通过被调度器调度后（跳到 thread.ra），开始执行并返回 0
    return new_pid;
}

int64_t sys_write(uint64_t fd, const char *buf, uint64_t len)
{
    int64_t ret;
    struct file *file = &(current->files->fd_array[fd]);
    if (file->opened == 0)
    {
        printk("file not opened\n");
        return ERROR_FILE_NOT_OPEN;
    }
    else
    {
        // check perms and call write function of file
        if (file->perms & FILE_WRITABLE)
        {
            ret = file->write(file, buf, len);
        }
        else
        {
            printk("file not writable\n");
            return -1;
        }
    }
    return ret;
}

int64_t sys_read(uint64_t fd, char *buf, uint64_t len)
{
    int64_t ret;
    struct file *file = &(current->files->fd_array[fd]);
    if (file->opened == 0)
    {
        printk("file not opened\n");
        return ERROR_FILE_NOT_OPEN;
    }
    else
    {
        // check perms and call read function of file
        if (file->perms & FILE_READABLE)
        {
            ret = file->read(file, buf, len);
        }
        else
        {
            printk("file not readable\n");
            return -1;
        }
    }
    return ret;
}

void do_syscall(struct pt_regs *regs)
{
#ifdef DEBUG
    printk("systemcall: ");
#endif
    switch (regs->x[16]) // syscall a7 -> x17 -> x[16]
    {
    case SYS_WRITE:
#ifdef DEBUG
        printk("write: fd = %d, buf = %p, count = %d\n", regs->x[9], regs->x[10], regs->x[11]);
#endif
        regs->x[9] = sys_write(regs->x[9], (const char *)regs->x[10], regs->x[11]);
        break;
    case SYS_READ:
#ifdef DEBUG
        printk("read: fd = %d, buf = %p, count = %d\n", regs->x[9], regs->x[10], regs->x[11]);
#endif
        regs->x[9] = sys_read(regs->x[9], (char *)regs->x[10], regs->x[11]);
        break;
    case SYS_GETPID:
#ifdef DEBUG
        printk("getpid\n");
#endif
        regs->x[9] = current->pid;
        break;
    case SYS_CLONE:
#ifdef DEBUG
        printk("clone\n");
#endif
        regs->x[9] = do_fork(regs);
        break;
    default:
        Err("Unimplemented system call: %d\n", regs->x[16]);
        break;
    }
    // 针对系统调用这一类异常，我们需要手动完成 sepc + 4
    regs->sepc += 4;
}
