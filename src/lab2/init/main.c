#include "printk.h"
#include "sbi.h"
#include "defs.h"

extern void test();

int start_kernel() {
    printk("2024");
    printk(" ZJU Operating System\n");

    test();
    return 0;
}
