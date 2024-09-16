#include "printk.h"
#include "sbi.h"
#include "defs.h"

extern void test();

#define csr_print(csr)                      \
({                                          \
    printk(#csr ":\t");                      \
    long __v = csr_read(csr);               \
    printk("%lx\n", __v);                    \
})

int start_kernel() {
    printk("2024");
    printk(" ZJU Operating System\n");

    csr_print(sstatus);
    csr_print(sie);
    csr_print(stvec);
    csr_print(scause);
    csr_print(sepc);

    test();
    return 0;
}
