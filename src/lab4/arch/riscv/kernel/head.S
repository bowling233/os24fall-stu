.extern start_kernel
.extern sbi_ecall
.extern _traps
.extern mm_init
.extern task_init
.extern setup_vm
.extern setup_vm_final

    .section .text.init
    .globl _start
_start:
    la sp, boot_stack_top

    call setup_vm
    call relocate

    call mm_init
    call task_init

    call setup_vm_final

    # set stvec = _traps
    la t0, _traps
    csrw stvec, t0 # csrrw x0, stvec, t0: stvec -> x0, t0 -> stvec

    # set sie[STIE] = 1
    li t0, 0x00000020 # bit 5
    csrs sie, t0 # csrrs x0, sie, t0: sie -> x0, x0 | t0 -> sie

    # set first time interrupt
    rdtime a0
    li a1, 10000000
    add a0, a0, a1 # add 1s
#if defined(USE_SD)
.equ QEMU_MTIMECMP_BASE, 0x2004000
    #if defined(USE_MHARTID)
    csrr t1, mhartid # [Illegal Instruction Fault] get hartid
    slli t1, t1, 3 # hartid * 8
    li t2, QEMU_MTIMECMP_BASE # CLINT + 4000
    add t1, t2, t1 # mtimecmp in QEMU = CLINT + 4000 + 8 * hartid
    #else
    li t1, QEMU_MTIMECMP_BASE # [Store/AMO Access Fault] Default mtimecmp address in QEMU
    #endif
    # store by yourself
    sd a0, 0(t1) # store timer to mtimecmp
#else
    # or choose use sbi_ecall
    addi a7, x0, 0x0 # EID #0x00: sbi_set_timer
    ecall
#endif

    # set sstatus[SIE] = 1
    li t0, 0x00000002 # bit 1
    csrs sstatus, t0 # csrrs x0, sstatus, t0: sstatus -> x0, x0 | t0 -> sstatus

    call start_kernel

relocate:
    .set BASE_ADDR, 0xffffffe000200000
    li t1, BASE_ADDR
    la t0, _start
    sub t0, t1, t0
    # set ra = ra + PA2VA_OFFSET
    add ra, ra, t0
    # set sp = sp + PA2VA_OFFSET (If you have set the sp before)
    add sp, sp, t0

    # set satp with early_pgtbl
    .extern early_pgtbl
    la t0, early_pgtbl
    srl t0, t0, 12 # t0 = t0 >> 12
    li t1, 0x8000000000000000 # SATP_MODE_SV39
    or t0, t0, t1 # t0 = t0 | t1

    sfence.vma zero, zero
    csrw satp, t0 # csrrw x0, satp, t0: satp -> x0, t0 -> satp

    ret

    .section .bss.stack
    .globl boot_stack
boot_stack:
    .space 4096 # <-- change to your stack size

    .globl boot_stack_top
boot_stack_top: