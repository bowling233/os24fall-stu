.extern start_kernel

    .section .text.init
    .globl _start
_start:
    # ----------------------
    # -  initialize stack  -
    # ----------------------
    la sp, boot_stack_top
    # YOUR CODE HERE

    # ----------------------

        # set stvec = _traps

    # ----------------------

        # set sie[STIE] = 1

    # ----------------------

        # set first time interrupt

    # ----------------------

        # set sstatus[SIE] = 1

    # ----------------------

    call start_kernel

    .section .bss.stack
    .globl boot_stack
boot_stack:
    .space 4096 # <-- change to your stack size

    .globl boot_stack_top
boot_stack_top: