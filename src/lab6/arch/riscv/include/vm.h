#ifndef __VM_H__
#define __VM_H__

struct mm_struct
{
        struct vm_area_struct *mmap;
};

struct vm_area_struct
{
        struct mm_struct *vm_mm;                  // 所属的 mm_struct
        uint64_t vm_start;                        // VMA 对应的用户态虚拟地址的开始
        uint64_t vm_end;                          // VMA 对应的用户态虚拟地址的结束
        struct vm_area_struct *vm_next, *vm_prev; // 链表指针
        uint64_t vm_flags;                        // VMA 对应的 flags
        // struct file *vm_file;    // 对应的文件（目前还没实现，而且我们只有一个 uapp 所以暂不需要）
        uint64_t vm_pgoff;  // 如果对应了一个文件，那么这块 VMA 起始地址对应的文件内容相对文件起始位置的偏移量
        uint64_t vm_filesz; // 对应的文件内容的长度
};

void create_mapping(uint64_t *pgtbl, uint64_t va, uint64_t pa, uint64_t sz, uint64_t perm);
void setup_vm_final(void);
uint64_t *sv39_pg_dir_dup(uint64_t *pgtbl);
uint64_t *find_pte(uint64_t*pgtbl, uint64_t va);

/*
 * @mm       : current thread's mm_struct
 * @addr     : the va to look up
 *
 * @return   : the VMA if found or NULL if not found
 */
struct vm_area_struct *find_vma(struct mm_struct *mm, uint64_t addr);

/*
 * @mm       : current thread's mm_struct
 * @addr     : the suggested va to map
 * @len      : memory size to map
 * @vm_pgoff : phdr->p_offset
 * @vm_filesz: phdr->p_filesz
 * @flags    : flags for the new VMA
 *
 * @return   : start va
 */
uint64_t do_mmap(struct mm_struct *mm, uint64_t addr, uint64_t len, uint64_t vm_pgoff, uint64_t vm_filesz, uint64_t flags);

#endif