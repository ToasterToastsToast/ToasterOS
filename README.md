# ToasterOS

ECNU Operating System 2025 Fall Final Project 

**Contributors**: ToasterToasterToast, syqwq

--- 

## 0x01. features
- currently none 🥹

## 0x02. lab-1
1. 首先，通过 `entry.S` 和 `kernel.ld` 的配合，我们得以在 M-mode 的权限下到达 `start.c` 的位置，接下来我们的任务是切换至 S-mode 并进入 `main.c` 中的 `main()` 函数。
由于 riscv 中没有提供“直接跳转并降级”的指令，我们需要使用陷阱机制。当发生外部中断或者异常后者系统调用的时候，CPU会陷入更高权限的模式去处理。处理完这些事件后，系统都需要从高权限模式返回到之前的低权限模式。
我们可以利用这种异常处理的机制，通过手动设置用来指示 mode 权限的寄存器欺骗 CPU 我们原来的权限就是 S-mode，然后触发“从陷阱返回”也就是 `mret` 指令来实现从 M-mode 到 S-mode 的切换。同时，执行 `mret` 指令时会跳转到 `mepc` 寄存器中存储的地址，因此我们只需要将 `mepc` 设置为 `main()` 函数的入口地址即可。

2. 

## 0xff. references
- [labs assignments](https://gitee.com/xu-ke-123/ecnu-oslab-2025-task)
- [riscv简单常用汇编指令xv6](https://blog.csdn.net/surfaceyan/article/details/135030477)