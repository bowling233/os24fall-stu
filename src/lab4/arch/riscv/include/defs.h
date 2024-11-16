#ifndef __DEFS_H__
#define __DEFS_H__

#include "stdint.h"

#define csr_read(csr)                       \
({                                          \
    register uint64_t __v;                    \
    asm volatile ("csrr %0, " #csr          \
                    : "=r" (__v));          \
    __v;                                    \
})

#define csr_write(csr, val)                                    \
  ({                                                           \
    uint64_t __v = (uint64_t)(val);                            \
    asm volatile("csrw " #csr ", %0" : : "r"(__v) : "memory"); \
  })

#define csr_print(csr)                      \
({                                          \
    printk(#csr ":\t");                      \
    long __v = csr_read(csr);               \
    printk("%lx\n", __v);                    \
})

// lab2

#define PHY_START 0x0000000080000000
#define PHY_SIZE 128 * 1024 * 1024 // 128 MiB，QEMU 默认内存大小
#define PHY_END (PHY_START + PHY_SIZE)

#define PGSIZE 0x1000 // 4 KiB
#define PGROUNDUP(addr) ((addr + PGSIZE - 1) & (~(PGSIZE - 1)))
#define PGROUNDDOWN(addr) (addr & (~(PGSIZE - 1)))

// lab3

#define OPENSBI_SIZE (0x200000)

#define VM_START (0xffffffe000000000)
#define VM_END   (0xffffffff00000000)
#define VM_SIZE (VM_END - VM_START)

#define PA2VA_OFFSET (VM_START - PHY_START)
// sv39
#define PTE_V (1 << 0)
#define PTE_R (1 << 1)
#define PTE_W (1 << 2)
#define PTE_X (1 << 3)
#define VPN0(vpn) ((vpn) << 12)
#define VPN1(vpn) ((vpn) << 21)
#define VPN2(vpn) ((vpn) << 30)
#define VA2VPN0(addr) (((addr) >> 12) & 0x1ff)
#define VA2VPN1(addr) (((addr) >> 21) & 0x1ff)
#define VA2VPN2(addr) (((addr) >> 30) & 0x1ff)
#define PPN0(ppn) ((ppn) << 10)
#define PPN1(ppn) ((ppn) << 19)
#define PPN2(ppn) ((ppn) << 28)
#define PA2PPN0(addr) (((addr) >> 12) & 0x1ff)
#define PA2PPN1(addr) (((addr) >> 21) & 0x1ff)
#define PA2PPN2(addr) (((addr) >> 30) & 0x3ffffff)
#define PTE_IS_VALID(pte) ((pte) & PTE_V)
#define PA2PTE(addr) (((addr) >> 2) & 0x003ffffffffffc00)
#define PTE2PA(pte) (((pte) & 0x003ffffffffffc00) << 2)
#define PTE2VA(pte) (PA2VA_OFFSET + PTE2PA(pte))
#define VA2PA(addr) ((addr) - PA2VA_OFFSET)
#define VA2PTE(addr) (PA2PTE(VA2PA(addr)))

#define SATP_SV39 (8L << 60)
#define SATP_PPN(addr) (((addr) >> 12) & 0xfffffffffff)

// lab4

#define USER_START (0x0000000000000000) // user space start virtual address
#define USER_END (0x0000004000000000) // user space end virtual address
#define SPP (1L << 8)
#define SPIE (1L << 5)
#define SUM (1L << 18)

#endif
