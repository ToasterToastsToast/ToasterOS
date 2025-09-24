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

2. 对于自旋锁是用于处理资源竞争的一个机制。在操作系统刚启动时，只有一个cpu的核在做booting的操作，因此这个时候不需要考虑使用自旋锁，且此时使用自旋锁可能会出现初始化的问题。为此，我们使用 `print_locking` 变量来标记当前是否处于初始化阶段，若是则不使用自旋锁。可以看见在 `main()` 函数中，我们将 cpu-0 作为初始化的cpu，并且在分配完之后，调用 `print_init()` 来对锁进行初始化。初始化完成之后，进行另一个核的各种初始化。因此在初始化阶段，只有他的 `printf` 语句在工作，而后，我们可以看见，领个核都在工作。初始化锁的时候，默认他的所有者为 -1，表示没有所有者。在进行锁的竞争的时候，逻辑是，不断检测锁的状态，若是锁被占用，则继续等待，若是锁空闲，则将锁的状态设置为占用，并且将所有者设置为当前 cpu 的 id。最初使用 `while` 语句，然后等待结束之后，对所有者进行赋值，但是由于 `while` 不是原子操作（不会被打断的最小操作）在等待和设置的间隙，依然可能发生竞争问题，因此，引入 `__sync_lock_test_and_set()` 函数，这是 gdb 提供的原子操作函数，可以保证在执行这个函数的时候不会被打断，从而避免竞争问题。对于解锁的过程，类似，将锁的持有者标记为 -1. 对于 `printf` 函数，我们在需要占用输出资源的时候进行申请上锁（注意判断此时锁是否启用），用完之后进行解锁。类似的，我们也可以实现 `putchar` 和 `puts` 函数

## 0xff. references
- [labs assignments](https://gitee.com/xu-ke-123/ecnu-oslab-2025-task)
- [riscv简单常用汇编指令xv6](https://blog.csdn.net/surfaceyan/article/details/135030477)