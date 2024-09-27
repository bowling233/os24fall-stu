.extern trap_handler
    .section .text.entry
    .align 2
    .globl _traps 
_traps:
    # YOUR CODE HERE
    # -----------

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

    # -----------

        # 2. call trap_handler

    csrr a0, scause
    call trap_handler

    # -----------

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

    # -----------

        # 4. return from trap

    # -----------

    sret

    .extern dummy
    .globl __dummy
    # special return function for the first time thread sched
__dummy:
    la t0, dummy
    csrw sepc, t0
    sret

.macro savePrev n
    sd s\n, \n*8+16(a0)
.endm

.macro loadNext n
    ld s\n, \n*8+16(a1)
.endm

    .globl __switch_to
    # extern void __switch_to(struct task_struct *prev, struct task_struct *next);
__switch_to:
    # save state to prev process
    sd ra, 32(a0) # a0->ra = ra
    sd sp, 40(a0) # a0->sp = sp
    sd s0, 48(a0) # a0->s0 = s0
    sd s1, 56(a0) # a0->s1 = s1
    sd s2, 64(a0) # a0->s2 = s2
    sd s3, 72(a0) # a0->s3 = s3
    sd s4, 80(a0) # a0->s4 = s4
    sd s5, 88(a0) # a0->s5 = s5
    sd s6, 96(a0) # a0->s6 = s6
    sd s7, 104(a0) # a0->s7 = s7
    sd s8, 112(a0) # a0->s8 = s8
    sd s9, 120(a0) # a0->s9 = s9
    sd s10, 128(a0) # a0->s10 = s10
    sd s11, 136(a0) # a0->s11 = s11

    # restore state from next process
    ld ra, 32(a1) # ra = a1->ra
    ld sp, 40(a1) # sp = a1->sp
    ld s0, 48(a1) # s0 = a1->s0
    ld s1, 56(a1) # s1 = a1->s1
    ld s2, 64(a1) # s2 = a1->s2
    ld s3, 72(a1) # s3 = a1->s3
    ld s4, 80(a1) # s4 = a1->s4
    ld s5, 88(a1) # s5 = a1->s5
    ld s6, 96(a1) # s6 = a1->s6
    ld s7, 104(a1) # s7 = a1->s7
    ld s8, 112(a1) # s8 = a1->s8
    ld s9, 120(a1) # s9 = a1->s9
    ld s10, 128(a1) # s10 = a1->s10
    ld s11, 136(a1) # s11 = a1->s11

    ret