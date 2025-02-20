#include "stdint.h"
#include "printk.h"
#include "proc.h"
#include "syscall.h"
#include "vm.h"
#include "mm.h"
#include "defs.h"
#include "string.h"

void clock_set_next_event();
extern char __ret_from_fork[];

struct pt_regs
{
    uint64_t x[31]; // x1-x31
    uint64_t sepc;
    // where is sstatus?
};

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

void do_page_fault(struct pt_regs *regs, uint64_t stval, uint64_t scause)
{
#ifdef DEBUG
    Log("pc: %lx, stval: %lx", regs->sepc, stval);
#endif
    // 通过 stval 获得访问出错的虚拟内存地址（Bad Address）
    // 通过 find_vma() 查找 bad address 是否在某个 vma 中
    struct vm_area_struct *vma = find_vma(&current->mm, stval);
    // 如果不在，则出现非预期错误，可以通过 Err 宏输出错误信息
    if (!vma)
    {
        Log("current vma: %lx", current->mm.mmap);
        for (struct vm_area_struct *vma = current->mm.mmap; vma; vma = vma->vm_next)
        {
            Log("vma: %lx %lx %lx %lx %lx", vma->vm_start, vma->vm_end, vma->vm_pgoff, vma->vm_filesz, vma->vm_flags);
        }
        Err("VMA not found");
    }
    // 如果在，则根据 vma 的 flags 权限判断当前 page fault 是否合法
    // 如果非法（比如触发的是 instruction page fault 但 vma 权限不允许执行），则 Err 输出错误信息
    switch (scause)
    {
    case 0x000000000000000C:
        if (!(vma->vm_flags & VM_EXEC))
        {
            Err("VMA not executable");
        }
        break;
    case 0x000000000000000D:
        if (!(vma->vm_flags & VM_READ))
        {
            Err("VMA not readable");
        }
        break;
    case 0x000000000000000F:
        if (!(vma->vm_flags & VM_WRITE))
        {
            Err("VMA not writable");
        }
        else
        {
            // 如果发生了写错误，且 vma 的 VM_WRITE 位为 1，而且对应地址有 pte（进行了映射）但 pte 的 PTE_W 位为 0，那么就可以断定这是一个写时复制的页面，我们只需要在这个时候拷贝一份原来的页面，重新创建一个映射即可。
            uint64_t *pte_p = find_pte(current->pgd, stval);
            uint64_t pte = pte_p ? *pte_p : 0;
            if (!pte) // no PTE, not COW
                break;

            if ((pte & PTE_W) != 0) // PTE_W is not 0, not COW
                break;

#ifdef DEBUG
            Log("COW");
#endif
            // 拷贝了页面之后，别忘了将原来的页面引用计数减一。这样父子进程想要写入的时候，都会触发 COW，并拷贝一个新页面，都拷贝完成后，原来的页面将自动 free 掉。
            // 进一步的，父进程 COW 后，子进程再进行写入的时候，也可以在这时判断引用计数，如果计数为 1，说明这个页面只有一个引用，那么就可以直接将 pte 的 PTE_W 位再置 1，这样就可以直接写入了，免去一次额外的复制。
            if (get_page_refcnt((void *)PTE2VA(pte)) > 1) // cow
            {
#ifdef DEBUG
                Log("page copy");
#endif
                void *old_page = (void *)PTE2VA(pte);
                uint64_t old_flags = pte & PTE_FLAGS_MASK;
                void *new_page = alloc_page();
                uint64_t new_flags = old_flags | PTE_W;
                memcpy(new_page, old_page, PGSIZE);
                create_mapping(current->pgd, PGROUNDDOWN(stval), VA2PA((uint64_t)new_page), PGSIZE, new_flags);
                put_page(old_page);
            }
            else // direct write
            {
#ifdef DEBUG
                Log("direct write");
#endif
                *pte_p = pte | PTE_W;
            }
            return;
        }
        break;
    default:
        Err("unknown page fault");
        break;
    }
#ifdef DEBUG
    Log("vma flags: VM_READ %d VM_WRITE %d VM_EXEC %d VM_ANON %d", vma->vm_flags & VM_READ, vma->vm_flags & VM_WRITE, vma->vm_flags & VM_EXEC, vma->vm_flags & VM_ANON);
#endif
    // 其他情况合法，需要我们按接下来的流程创建映射
    // 分配一个页，接下来要将这个页映射到对应的用户地址空间
    void *page = alloc_page();
    // 通过 (vma->vm_flags & VM_ANONYM) 获得当前的 VMA 是否是匿名空间
    uint64_t perm = ((vma->vm_flags & VM_WRITE) ? PTE_W : 0) | ((vma->vm_flags & VM_EXEC) ? PTE_X : 0) | ((vma->vm_flags & VM_READ) ? PTE_R : 0) | PTE_V | PTE_U;
    // 如果是匿名空间，则直接映射即可
    create_mapping(current->pgd, PGROUNDDOWN(stval), VA2PA((uint64_t)page), PGSIZE, perm);
    // 如果不是，则需要根据 vma->vm_pgoff 等信息从 ELF 中读取数据，填充后映射到用户空间
    if (!(vma->vm_flags & VM_ANON))
    {
        uint64_t target_offset = stval - vma->vm_start;
        uint64_t page_down_offset = PGROUNDDOWN(stval) - vma->vm_start;
        uint64_t page_up_offset = PGROUNDUP(stval) - vma->vm_start;
        if (page_down_offset >= vma->vm_filesz)
            memset(page, 0, PGSIZE);
        else if (page_up_offset <= vma->vm_filesz)
            memcpy(page, _sramdisk + vma->vm_pgoff + page_down_offset, PGSIZE);
        else
        {
            uint64_t zero_size = page_up_offset - vma->vm_filesz;
            memcpy(page, _sramdisk + vma->vm_pgoff + page_down_offset, PGSIZE - (zero_size));
            memset(page + vma->vm_filesz - page_down_offset, 0, zero_size);
        }
    }
}

void trap_handler(uint64_t scause, uint64_t sepc, struct pt_regs *regs, uint64_t stval)
{
    // 通过 `scause` 判断 trap 类型
    // 如果是 interrupt 判断是否是 timer interrupt
    // 如果是 timer interrupt 则打印输出相关信息，并通过 `clock_set_next_event()` 设置下一次时钟中断
    // `clock_set_next_event()` 见 4.3.4 节
    // 其他 interrupt / exception 可以直接忽略，推荐打印出来供以后调试
#ifdef DEBUG
    switch (scause)
    {
    case 0x800000000000000b:
        printk("[interrupt] Machine External, ");
        break;
    case 0x8000000000000009:
        printk("[interrupt] Supervisor External, ");
        break;
    case 0x8000000000000007:
        printk("[interrupt] Machine Timer, ");
        break;
    case 0x8000000000000005:
        printk("[interrupt] Supervisor Timer, ");
        break;
    case 0x8000000000000003:
        printk("[interrupt] Machine Software, ");
        break;
    case 0x8000000000000001:
        printk("[interrupt] Supervisor Software, ");
        break;
    case 0x0000000000000000:
        printk(RED "[exception] Instruction address misaligned, ");
        break;
    case 0x0000000000000001:
        printk(RED "[exception] Instruction access fault, ");
        break;
    case 0x0000000000000002:
        printk(RED "[exception] Illegal instruction, ");
        break;
    case 0x0000000000000003:
        printk(RED "[exception] Breakpoint, ");
        break;
    case 0x0000000000000004:
        printk(RED "[exception] Load address misaligned, ");
        break;
    case 0x0000000000000005:
        printk(RED "[exception] Load access fault, ");
        break;
    case 0x0000000000000006:
        printk(RED "[exception] Store/AMO address misaligned, ");
        break;
    case 0x0000000000000007:
        printk(RED "[exception] Store/AMO access fault, ");
        break;
    case 0x0000000000000008:
        printk(RED "[exception] Environment call from U-mode, ");
        break;
    case 0x0000000000000009:
        printk(RED "[exception] Environment call from S-mode, ");
        break;
    case 0x000000000000000C:
        printk(RED "[exception] Instruction page fault, ");
        break;
    case 0x000000000000000D:
        printk(RED "[exception] Load page fault, ");
        break;
    case 0x000000000000000F:
        printk(RED "[exception] Store/AMO page fault, ");
        break;
    case 0x0000000000000012:
        printk(RED "[exception] Software check, ");
        break;
    case 0x0000000000000013:
        printk(RED "[exception] Hardware error, ");
        break;
    default:
        if (scause & 0x8000000000000000)
            printk(RED "[interrupt] unknown interrupt: %x, ", scause);
        else
            printk(RED "[exception] unknown exception: %x, ", scause);
        break;
    }
    printk("sepc: %lx\n" CLEAR, sepc);
#endif

    switch (scause)
    {
    case 0x8000000000000007:
    case 0x8000000000000005:
        clock_set_next_event();
        do_timer();
        break;
    case 0x0000000000000008:
#ifdef DEBUG
        printk("systemcall: ");
#endif
        switch (regs->x[16]) // syscall a7 -> x17 -> x[16]
        {
        case __NR_write:
#ifdef DEBUG
            printk("write: fd = %d, buf = %p, count = %d\n", regs->x[10], regs->x[11], regs->x[12]);
#endif
            regs->x[9] = sys_write(regs->x[9], (void *)regs->x[10], regs->x[11]);
            break;
        case __NR_getpid:
#ifdef DEBUG
            printk("getpid\n");
#endif
            regs->x[9] = sys_getpid();
            break;
        case __NR_clone:
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
        break;
    case 0x000000000000000C:
    case 0x000000000000000D:
    case 0x000000000000000F:
        do_page_fault(regs, stval, scause);
        break;
    default:
        Err("Unhandled exception/interupt");
        break;
    }
}
