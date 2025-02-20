#include "stdint.h"
#include "printk.h"
#include "proc.h"
#include "syscall.h"
#include "vm.h"
#include "mm.h"
#include "defs.h"
#include "string.h"

void clock_set_next_event();

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
        Log("[interrupt] Machine External, ");
        break;
    case 0x8000000000000009:
        Log("[interrupt] Supervisor External, ");
        break;
    case 0x8000000000000007:
        Log("[interrupt] Machine Timer, ");
        break;
    case 0x8000000000000005:
        Log("[interrupt] Supervisor Timer, ");
        break;
    case 0x8000000000000003:
        Log("[interrupt] Machine Software, ");
        break;
    case 0x8000000000000001:
        Log("[interrupt] Supervisor Software, ");
        break;
    case 0x0000000000000000:
        Log("[exception] Instruction address misaligned, ");
        break;
    case 0x0000000000000001:
        Log("[exception] Instruction access fault, ");
        break;
    case 0x0000000000000002:
        Log("[exception] Illegal instruction, ");
        break;
    case 0x0000000000000003:
        Log("[exception] Breakpoint, ");
        break;
    case 0x0000000000000004:
        Log("[exception] Load address misaligned, ");
        break;
    case 0x0000000000000005:
        Log("[exception] Load access fault, ");
        break;
    case 0x0000000000000006:
        Log("[exception] Store/AMO address misaligned, ");
        break;
    case 0x0000000000000007:
        Log("[exception] Store/AMO access fault, ");
        break;
    case 0x0000000000000008:
        Log("[exception] Environment call from U-mode, ");
        break;
    case 0x0000000000000009:
        Log("[exception] Environment call from S-mode, ");
        break;
    case 0x000000000000000C:
        Log("[exception] Instruction page fault, ");
        break;
    case 0x000000000000000D:
        Log("[exception] Load page fault, ");
        break;
    case 0x000000000000000F:
        Log("[exception] Store/AMO page fault, ");
        break;
    case 0x0000000000000012:
        Log("[exception] Software check, ");
        break;
    case 0x0000000000000013:
        Log("[exception] Hardware error, ");
        break;
    default:
        if (scause & 0x8000000000000000)
        {
            Log("[interrupt] unknown interrupt: %x, ", scause);
        }
        else
        {
            Log("[exception] unknown exception: %x, ", scause);
        }
        break;
    }
    Log("sepc: %lx\n" CLEAR, sepc);
#endif

    switch (scause)
    {
    case 0x8000000000000007:
    case 0x8000000000000005:
        clock_set_next_event();
        do_timer();
        break;
    case 0x0000000000000008:
        do_syscall(regs);
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
