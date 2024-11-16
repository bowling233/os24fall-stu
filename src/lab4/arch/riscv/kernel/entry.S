.altmacro
.extern trap_handler
    .section .text.entry
    .align 2
    .globl _traps
_traps:
    # 进入 trap 的时候需要切换到内核栈
    # 如果是内核线程（没有用户栈）触发了异常，则不需要进行切换。（内核线程的 sp 永远指向的内核栈，且 sscratch 为 0）
    csrr t0, sscratch
    beqz t0, 1f
    csrw sscratch, sp
    mv sp, t0
1:

    # 1. save 32 registers and sepc to stack

    addi sp, sp, -8*32 # only need 31, but risc-v requires 16-byte alignment for sp
    # no need to save x0
    sd x1, 0(sp)
    # no need to save x2(sp)
    sd x3, 16(sp)
    sd x4, 24(sp)
    sd x5, 32(sp)
    sd x6, 40(sp)
    sd x7, 48(sp)
    sd x8, 56(sp)
    sd x9, 64(sp)
    sd x10, 72(sp)
    sd x11, 80(sp)
    sd x12, 88(sp)
    sd x13, 96(sp)
    sd x14, 104(sp)
    sd x15, 112(sp)
    sd x16, 120(sp)
    sd x17, 128(sp)
    sd x18, 136(sp)
    sd x19, 144(sp)
    sd x20, 152(sp)
    sd x21, 160(sp)
    sd x22, 168(sp)
    sd x23, 176(sp)
    sd x24, 184(sp)
    sd x25, 192(sp)
    sd x26, 200(sp)
    sd x27, 208(sp)
    sd x28, 216(sp)
    sd x29, 224(sp)
    sd x30, 232(sp)
    sd x31, 240(sp)
    csrr a1, sepc
    sd a1, 248(sp) # the last one is sepc

    # 2. call trap_handler

    csrr a0, scause
    mv a2, sp
    call trap_handler

    # 3. restore sepc and 32 registers (x2(sp) should be restore last) from stack

    ld a1, 248(sp) # sepc
    csrw sepc, a1
    ld x1, 0(sp)
    # x2(sp) will be restored last
    ld x3, 16(sp)
    ld x4, 24(sp)
    ld x5, 32(sp)
    ld x6, 40(sp)
    ld x7, 48(sp)
    ld x8, 56(sp)
    ld x9, 64(sp)
    ld x10, 72(sp)
    ld x11, 80(sp)
    ld x12, 88(sp)
    ld x13, 96(sp)
    ld x14, 104(sp)
    ld x15, 112(sp)
    ld x16, 120(sp)
    ld x17, 128(sp)
    ld x18, 136(sp)
    ld x19, 144(sp)
    ld x20, 152(sp)
    ld x21, 160(sp)
    ld x22, 168(sp)
    ld x23, 176(sp)
    ld x24, 184(sp)
    ld x25, 192(sp)
    ld x26, 200(sp)
    ld x27, 208(sp)
    ld x28, 216(sp)
    ld x29, 224(sp)
    ld x30, 232(sp)
    ld x31, 240(sp)
    addi sp, sp, 8*32

    # 处理完成后需要再切换回来
    # 如果是内核线程（没有用户栈）触发了异常，则不需要进行切换。
    csrr t0, sscratch
    beqz t0, 2f
    csrw sscratch, sp
    mv sp, t0
2:

    # 4. return from trap

    sret

    .extern dummy
    .globl __dummy
    # special return function for the first time thread sched
__dummy:
    la t0, dummy
    csrw sepc, t0
    # 在 __dummy 进入用户态模式的时候，我们需要切换这两个栈
    mv t0, sp
    csrr sp, sscratch
    csrw sscratch, t0
    sret

    .globl __switch_to
    # extern void __switch_to(struct task_struct *prev, struct task_struct *next);
__switch_to:
    # save state to prev process
    sd ra, 32(a0) # a0->ra = ra
    sd sp, 40(a0) # a0->sp = sp
    .rept 12
      sd s\+, \+*8+48(a0) # a0->s0-s11 = s0-s11
    .endr
    csrr t0, sepc
    sd t0, 144(a0) # a0->sepc = sepc
    csrr t0, sstatus
    sd t0, 152(a0) # a0->sstatus = sstatus
    csrr t0, sscratch
    sd t0, 160(a0) # a0->sscratch = sscratch
    csrr t0, satp
    sd t0, 168(a0) # a0->satp = satp

    # restore state from next process
    ld ra, 32(a1) # ra = a1->ra
    ld sp, 40(a1) # sp = a1->sp
    .rept 12
      ld s\+, \+*8+48(a1) # s0-s11 = a1->s0-s11
    .endr
    ld t0, 144(a1) # sepc = a1->sepc
    csrw sepc, t0
    ld t0, 152(a1) # sstatus = a1->sstatus
    csrw sstatus, t0
    ld t0, 160(a1) # sscratch = a1->sscratch
    csrw sscratch, t0
    ld t0, 168(a1) # satp = a1->satp
    csrw satp, t0
    sfence.vma
    fence.i

    ret