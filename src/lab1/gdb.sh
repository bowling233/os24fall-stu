target remote :1234
layout asm
layout regs
b start_kernel
b _traps
b trap_handler
b _start
b sbi_ecall