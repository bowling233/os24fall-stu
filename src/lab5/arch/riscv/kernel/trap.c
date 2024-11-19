#include "stdint.h"
#include "printk.h"
#include "proc.h"
#include "syscall.h"
#include "vm.h"
#include "mm.h"
#include "defs.h"
#include "string.h"

extern struct task_struct *current;
extern char _sramdisk[];
void clock_set_next_event();

struct pt_regs
{
    uint64_t x[31]; // x1-x31
    uint64_t sepc;
    // where is sstatus?
};

void do_page_fault(struct pt_regs *regs, uint64_t stval, uint64_t scause) {
#ifdef DEBUG
    Log("stval: %lx", stval);
#endif
    // 通过 stval 获得访问出错的虚拟内存地址（Bad Address）
    // 通过 find_vma() 查找 bad address 是否在某个 vma 中
    struct vm_area_struct *vma = find_vma(&current->mm, stval);
    // 如果不在，则出现非预期错误，可以通过 Err 宏输出错误信息
    if (!vma)
    {
        Err("page fault at %lx, bad address %lx", regs->sepc, stval);
    }
    // 如果在，则根据 vma 的 flags 权限判断当前 page fault 是否合法
    // 如果非法（比如触发的是 instruction page fault 但 vma 权限不允许执行），则 Err 输出错误信息
    switch(scause)
    {
        case 0x000000000000000C:
            if (!(vma->vm_flags & VM_EXEC))
            {
                Err("instruction page fault at %lx, bad address %lx", regs->sepc, stval);
            }
            break;
        case 0x000000000000000D:
            if (!(vma->vm_flags & VM_READ))
            {
                Err("load page fault at %lx, bad address %lx", regs->sepc, stval);
            }
            break;
        case 0x000000000000000F:
            if (!(vma->vm_flags & VM_WRITE))
            {
                Err("store page fault at %lx, bad address %lx", regs->sepc, stval);
            }
            break;
        default:
            Err("unknown page fault at %lx, bad address %lx", regs->sepc, stval);
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
    if(!(vma->vm_flags & VM_ANON))
    {
        uint64_t target_offset = stval - vma->vm_start;
        uint64_t page_down_offset = PGROUNDDOWN(stval) - vma->vm_start;
        uint64_t page_up_offset = PGROUNDUP(stval) - vma->vm_start;
        if (page_down_offset >= vma->vm_filesz)
            memset(page, 0, PGSIZE);
        else if(page_up_offset <= vma->vm_filesz)
            memcpy(page, _sramdisk + vma->vm_pgoff + page_down_offset, PGSIZE);
        else
        {
            uint64_t zero_size = page_up_offset - vma->vm_filesz;
            memcpy(page, _sramdisk + vma->vm_pgoff + page_down_offset, PGSIZE - (zero_size));
            memset(page + vma->vm_filesz - page_down_offset, 0, PGSIZE - zero_size);
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
