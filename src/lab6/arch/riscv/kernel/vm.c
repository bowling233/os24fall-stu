#include "defs.h"
#include "mm.h"
#include "string.h"
#include "vm.h"
#include "printk.h"
#include "virtio.h"

void print_pgtbl(uint64_t *pgtbl)
{
    uint64_t *pgtbl1;
    uint64_t *pgtbl2;
    printk("page table %lx\n", pgtbl);
    for (int i = 0; i < 512; i++)
    {
        if (((uint64_t *)pgtbl)[i] != 0)
        {
            printk(" ..");
            printk(" %lx: pte %016lx pa %016lx\n", &pgtbl[i], ((uint64_t *)pgtbl)[i], PTE2PA(((uint64_t *)pgtbl)[i]));
            pgtbl1 = (uint64_t *)PTE2VA(((uint64_t *)pgtbl)[i]);
            for (int j = 0; j < 512; j++)
            {
                if (((uint64_t *)pgtbl1)[j] != 0)
                {
                    printk(" ....");
                    printk(" %lx: pte %016lx pa %016lx\n", &pgtbl1[j], ((uint64_t *)pgtbl1)[j], PTE2PA(((uint64_t *)pgtbl1)[j]));
                    pgtbl2 = (uint64_t *)PTE2VA(((uint64_t *)pgtbl1)[j]);
                    for (int k = 0; k < 512; k++)
                    {
                        if (((uint64_t *)pgtbl2)[k] != 0)
                        {
                            printk(" ......");
                            printk(" %lx: pte %016lx pa %016lx\n", &pgtbl2[k], ((uint64_t *)pgtbl2)[k], PTE2PA(((uint64_t *)pgtbl2)[k]));
                        }
                    }
                }
            }
        }
    }
}

/* early_pgtbl: 用于 setup_vm 进行 1GiB 的映射 */
uint64_t early_pgtbl[512] __attribute__((__aligned__(0x1000)));

void setup_vm(void)
{
    /*
     * 1. 由于是进行 1GiB 的映射，这里不需要使用多级页表
     * 2. 将 va 的 64bit 作为如下划分： | high bit | 9 bit | 30 bit |
     *     high bit 可以忽略
     *     中间 9 bit 作为 early_pgtbl 的 index
     *     低 30 bit 作为页内偏移，这里注意到 30 = 9 + 9 + 12，即我们只使用根页表，根页表的每个 entry 都对应 1GiB 的区域
     * 3. Page Table Entry 的权限 V | R | W | X 位设置为 1
     **/
    memset(early_pgtbl, 0x0, PGSIZE);
    early_pgtbl[VA2VPN2(VM_START)] = PPN2(PA2PPN2(PHY_START)) | PTE_V | PTE_R | PTE_W | PTE_X;
    early_pgtbl[VA2VPN2(PHY_START)] = PPN2(PA2PPN2(PHY_START)) | PTE_V | PTE_R | PTE_W | PTE_X;
    printk("...setup_vm done\n");
}

/* swapper_pg_dir: kernel pagetable 根目录，在 setup_vm_final 进行映射 */
uint64_t swapper_pg_dir[512] __attribute__((__aligned__(0x1000)));

extern char _stext[], _etext[], _srodata[], _erodata[], _sdata[], _edata[], _sbss[], _ebss[];

void setup_vm_final(void)
{
    memset(swapper_pg_dir, 0x0, PGSIZE);

    // No OpenSBI mapping required

    // mapping kernel text X|-|R|V
    create_mapping(swapper_pg_dir, (uint64_t)_stext, (uint64_t)_stext - PA2VA_OFFSET, (uint64_t)_etext - (uint64_t)_stext, PTE_R | PTE_X | PTE_V);

    // mapping kernel rodata -|-|R|V
    create_mapping(swapper_pg_dir, (uint64_t)_srodata, (uint64_t)_srodata - PA2VA_OFFSET, (uint64_t)_erodata - (uint64_t)_srodata, PTE_R | PTE_V);

    // mapping other memory -|W|R|V
    create_mapping(swapper_pg_dir, (uint64_t)_sdata, (uint64_t)_sdata - PA2VA_OFFSET, (uint64_t)(VM_START + PHY_SIZE) - (uint64_t)_sdata, PTE_R | PTE_W | PTE_V);

    // set satp with swapper_pg_dir
    csr_write(satp, (SATP_PPN(VA2PA((uint64_t)swapper_pg_dir)) | SATP_SV39));

    // lab6: virtio
    create_mapping(swapper_pg_dir, io_to_virt(VIRTIO_START), VIRTIO_START, VIRTIO_SIZE * VIRTIO_COUNT, PTE_W | PTE_R | PTE_V);

    // flush TLB
    asm volatile("sfence.vma zero, zero");

    // flush icache
    asm volatile("fence.i");
    printk("...setup_vm_final done!\n");
    return;
}

/* 创建多级页表映射关系 */
/* 不要修改该接口的参数和返回值 */
void create_mapping(uint64_t *pgtbl, uint64_t va, uint64_t pa, uint64_t sz, uint64_t perm)
{
#ifdef DEBUG
    Log("input va %lx pa %lx sz %lx perm %lx", va, pa, sz, perm);
#endif
    if (sz == 0)
    {
        return;
    }

    va = PGROUNDDOWN(va);
    pa = PGROUNDDOWN(pa);
#ifdef DEBUG
    Log("true va %lx pa %lx", va, pa);
#endif

    uint64_t vpn2 = VA2VPN2(va);
    uint64_t vpn1 = VA2VPN1(va);
    uint64_t vpn0 = VA2VPN0(va);

    for (; vpn2 <= VA2VPN2(va + sz - 1); vpn2++)
    {
        if (!PTE_IS_VALID(pgtbl[vpn2]))
        {
            pgtbl[vpn2] = VA2PTE((uint64_t)kalloc()) | PTE_V;
        }
        uint64_t *pgtbl1 = (uint64_t *)PTE2VA(pgtbl[vpn2]);
        for (; vpn2 == VA2VPN2(va + sz - 1) ? vpn1 <= VA2VPN1(va + sz - 1) : vpn1 < 512; vpn1++)
        {
            if (!PTE_IS_VALID(pgtbl1[vpn1]))
            {
                pgtbl1[vpn1] = VA2PTE((uint64_t)kalloc()) | PTE_V;
            }
            uint64_t *pgtbl0 = (uint64_t *)PTE2VA(pgtbl1[vpn1]);
            for (; vpn1 == VA2VPN1(va + sz - 1) ? vpn0 <= VA2VPN0(va + sz - 1) : vpn0 < 512; vpn0++)
            {
#ifdef DEBUG
                if (PTE_IS_VALID(pgtbl0[vpn0]))
                {
                    Log("pte %lx already exists, remapping", pgtbl0[vpn0]);
                }
#endif
                pgtbl0[vpn0] = PA2PTE(pa) | perm;
                pa += PGSIZE;
            }
            vpn0 = 0;
        }
        vpn1 = 0;
    }
}

uint64_t *sv39_pg_dir_dup(uint64_t *pgtbl)
{
#ifdef DEBUG
    Log("");
#endif
    uint64_t *new_pgtbl = (uint64_t *)alloc_page();
    memset(new_pgtbl, 0x0, PGSIZE);
    for (uint64_t vpn2 = 0; vpn2 < 512; vpn2++)
    {
        if (PTE_IS_VALID(pgtbl[vpn2]))
        {
            uint64_t *new_pgtbl1 = alloc_page();
            memset(new_pgtbl1, 0x0, PGSIZE);
            new_pgtbl[vpn2] = VA2PTE((uint64_t)new_pgtbl1) | PTE_V;
            uint64_t *pgtbl1 = (uint64_t *)PTE2VA(pgtbl[vpn2]);
            for (uint64_t vpn1 = 0; vpn1 < 512; vpn1++)
            {
                if (PTE_IS_VALID(pgtbl1[vpn1]))
                {
                    uint64_t *new_pgtbl0 = alloc_page();
                    memset(new_pgtbl0, 0x0, PGSIZE);
                    new_pgtbl1[vpn1] = VA2PTE((uint64_t)new_pgtbl0) | PTE_V;
                    uint64_t *pgtbl0 = (uint64_t *)PTE2VA(pgtbl1[vpn1]);
                    for (uint64_t vpn0 = 0; vpn0 < 512; vpn0++)
                    {
                        if (PTE_IS_VALID(pgtbl0[vpn0]))
                        {
                            new_pgtbl0[vpn0] = pgtbl0[vpn0];
                        }
                    }
                }
            }
        }
    }
    return new_pgtbl;
}

uint64_t *find_pte(uint64_t *pgtbl, uint64_t va)
{
    if (!pgtbl)
        return NULL;
    uint64_t vpn2 = VA2VPN2(va);
    if (!PTE_IS_VALID(pgtbl[vpn2]))
        return NULL;
    uint64_t *pgtbl1 = (uint64_t *)PTE2VA(pgtbl[vpn2]);
    if (!pgtbl1)
        return NULL;
    uint64_t vpn1 = VA2VPN1(va);
    if (!PTE_IS_VALID(pgtbl1[vpn1]))
        return NULL;
    uint64_t *pgtbl2 = (uint64_t *)PTE2VA(pgtbl1[vpn1]);
    if (!pgtbl2)
        return NULL;
    return &pgtbl2[VA2VPN0(va)];
}

struct vm_area_struct *find_vma(struct mm_struct *mm, uint64_t addr)
{
    struct vm_area_struct *vma = mm->mmap;
    while (vma != NULL)
    {
        if (addr >= vma->vm_start && addr < vma->vm_end)
        {
            return vma;
        }
        vma = vma->vm_next;
    }
    return NULL;
}

uint64_t do_mmap(struct mm_struct *mm, uint64_t addr, uint64_t len, uint64_t vm_pgoff, uint64_t vm_filesz, uint64_t flags)
{
#ifdef DEBUG
    Log("addr %lx len %lx vm_pgoff %lx vm_filesz %lx flags %lx", addr, len, vm_pgoff, vm_filesz, flags);
#endif
    // vma is page aligned
    // uint64_t start = PGROUNDDOWN(addr);
    // uint64_t end = PGROUNDUP(addr + len);
    // len = end - start;
    // uint64_t start_off = addr - start;
    // vm_pgoff -= start_off;
    // vm_filesz += start_off;

    struct vm_area_struct *vma = mm->mmap;
    struct vm_area_struct *prev = NULL;
    while (vma != NULL)
    {
        if (addr + len > vma->vm_start && addr < vma->vm_end)
        {
            // 请求的内存区域包含于已有的 VMA 中
            return -1;
        }
        if (addr < vma->vm_start)
        {
            // 请求的内存区域的起始地址位于已有的 VMA 之前
            break;
        }
        prev = vma;
        vma = vma->vm_next;
    }
    struct vm_area_struct *new_vma = (struct vm_area_struct *)kalloc();
    new_vma->vm_mm = mm;
    new_vma->vm_start = addr;
    new_vma->vm_end = addr + len;
    new_vma->vm_next = vma;
    new_vma->vm_prev = prev;
    new_vma->vm_flags = flags;
    new_vma->vm_pgoff = vm_pgoff;
    new_vma->vm_filesz = vm_filesz;
    if (prev != NULL)
    {
        prev->vm_next = new_vma;
    }
    else
    {
        mm->mmap = new_vma;
    }
    if (vma != NULL)
    {
        vma->vm_prev = new_vma;
    }
    return addr;
}
