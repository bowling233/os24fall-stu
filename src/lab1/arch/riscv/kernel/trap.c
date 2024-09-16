#include "stdint.h"
#include "printk.h"

void clock_set_next_event();

void trap_handler(uint64_t scause, uint64_t sepc)
{
    // 通过 `scause` 判断 trap 类型
    // 如果是 interrupt 判断是否是 timer interrupt
    // 如果是 timer interrupt 则打印输出相关信息，并通过 `clock_set_next_event()` 设置下一次时钟中断
    // `clock_set_next_event()` 见 4.3.4 节
    // 其他 interrupt / exception 可以直接忽略，推荐打印出来供以后调试
    switch(scause) {
    case 0x800000000000000b:
        printk("[interrupt] Machine External, ");
        break;
    case 0x8000000000000009:
        printk("[interrupt] Supervisor External, ");
        break;
    case 0x8000000000000007:
        printk("[interrupt] Machine Timer, ");
        clock_set_next_event();
        break;
    case 0x8000000000000005:
        printk("[interrupt] Supervisor Timer, ");
        clock_set_next_event();
        break;
    case 0x8000000000000003:
        printk("[interrupt] Machine Software, ");
        break;
    case 0x8000000000000001:
        printk("[interrupt] Supervisor Software, ");
        break;
    case 0x0000000000000000:
        printk("[exception] Instruction address misaligned, ");
        break;
    case 0x0000000000000001:
        printk("[exception] Instruction access fault, ");
        break;
    case 0x0000000000000002:
        printk("[exception] Illegal instruction, ");
        break; 
    case 0x0000000000000003:
        printk("[exception] Breakpoint, ");
        break;
    case 0x0000000000000004:
        printk("[exception] Load address misaligned, ");
        break;
    case 0x0000000000000005:
        printk("[exception] Load access fault, ");
        break;
    case 0x0000000000000006:
        printk("[exception] Store/AMO address misaligned, ");
        break;
    case 0x0000000000000007:
        printk("[exception] Store/AMO access fault, ");
        break;
    case 0x0000000000000008:
        printk("[exception] Environment call from U-mode, ");
        break;
    case 0x0000000000000009:
        printk("[exception] Environment call from S-mode, ");
        break;
    case 0x000000000000000A:
        printk("[exception] Environment call from M-mode, ");
        break;
    case 0x000000000000000B:
        printk("[exception] Instruction page fault, ");
        break;
    case 0x000000000000000C:
        printk("[exception] Load page fault, ");
        break;
    case 0x000000000000000E:
        printk("[exception] Store/AMO page fault, ");
        break;
    default:
        if(scause & 0x8000000000000000)
            printk("[interrupt] unknown interrupt: %x, ", scause);
        else
            printk("[exception] unknown exception: %x, ", scause);
        break;
    }
    printk("sepc: %x\n", sepc);
}