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
## 0xfe. extra experiments

### 4.1

#### 不上锁的情况

在题目给定的无锁情况中，实际结果里的`sum`并未达到2000000，这是因为调用的`sum++`不是一个原子操作。
它可以分解为三步：
- A 读 sum 的当前值到寄存器
- B 寄存器值加 1
- C 写回 sum
如果cpu0和cpu1同时分别执行$A_1 A_2 B_1 B_2 C_1 C_2$ 则sum结果上只增加了1，却经历了两次sum++。或者说，一些加法操作被覆盖了。

我们可以通过加锁，把求和操作变得原子化。

#### 细粒度锁的情况

```c
volatile static int started = 0;

volatile static int sum = 0;
static spinlock_t sum_lk;
int main()
{
    int cpuid = r_tp();
    if (cpuid == 0)
    {
        print_init();
        printf("cpu %d is booting!\n", cpuid);
        __sync_synchronize();
        started = 1;
        spinlock_init(&sum_lk,"sum");
        for (int i = 0; i < 1000000; i++){
            spinlock_acquire(&sum_lk);
            sum++;
            spinlock_release(&sum_lk);
        }

        printf("cpu %d report: sum = %d\n", cpuid, sum);
    }
    else
    {
        while (started == 0)
            ;
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        for (int i = 0; i < 1000000; i++){
            spinlock_acquire(&sum_lk);
            sum++;
            spinlock_release(&sum_lk);
        }

        printf("cpu %d report: sum = %d\n", cpuid, sum);
    }
    while (1)
        ;
}
```
输出：
```
cpu 0 is booting!
cpu 1 is booting!
cpu 0 report: sum = 1978168
cpu 1 report: sum = 2000000
```
通过把单个sum++原子化，可以保证两个cpu的相加不会互相覆盖，因此最终和为2000000。而两个cpu的求和操作是交替的，因此cpu0完成时，总求和次数应接近2000000，符合实验事实。

#### 粗粒度锁的情况
```c
volatile static int started = 0;

volatile static int sum = 0;
static spinlock_t sum_lk;
int main()
{
    int cpuid = r_tp();
    if (cpuid == 0)
    {
        print_init();
        printf("cpu %d is booting!\n", cpuid);
        __sync_synchronize();
        started = 1;
        spinlock_init(&sum_lk,"sum");
        spinlock_acquire(&sum_lk);
        for (int i = 0; i < 1000000; i++){

            sum++;

        }
        spinlock_release(&sum_lk);
        printf("cpu %d report: sum = %d\n", cpuid, sum);
    }
    else
    {
        while (started == 0)
            ;
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        spinlock_acquire(&sum_lk);
        for (int i = 0; i < 1000000; i++){

            sum++;

        }
        spinlock_release(&sum_lk);
        printf("cpu %d report: sum = %d\n", cpuid, sum);
    }
    while (1)
        ;
}
```
```
输出：
cpu 0 is booting!
cpu 1 is booting!
cpu 0 report: sum = 1000000
cpu 1 report: sum = 2000000
```
在循环外加锁，把整个求和循环原子化，则每个cpu执行各自的1000000次，符合实验现象。

#### 结论
在多核并发编程中，对共享资源的访问必须受到同步机制的保护，以避免数据竞争和结果不一致。
**锁的粒度**是一个关键的权衡点。**细粒度锁**提供了更高的并发性，但可能带来较大的锁竞争开销。**粗粒度锁**降低了锁的开销，但会限制并行性，可能导致性能瓶颈。

### 4.2
 在本实验中，删除了printf的自旋锁。即注释掉了两端对`spinlock_acquire`和`spinlock_release`的调用。
 主程序修改如下：
 ```c
 #include "arch/mod.h"
#include "lib/mod.h"

static volatile int started = 0;

int main() {
    int id = mycpuid();
    if (id == 0) {

        print_init();

        started = 1;
    } else {
        while (!started)
            ;
    }
    printf("cpu-%d is running! this is a long message which should be intertwined with that of another cpu, that happens because i disabled the spinlocks, WOW! THIS IS SO COOL! i am zhoujingrong and i did this experiment. the ultimate answer is 42 but i prefer 413.\n", id);

    return 0;
}
```
输出如下：
```
qemu-system-riscv64   -machine virt -bios none -kernel target/kernel/kernel-qemu.elf   -m 128M -smp 2 -nographic  
cpu-0c ipsu-1 is r running! this is a luong messagen which should be intertwined with that of another cpu, that happens because i disabled the spinlocks, WOW! THIS IS SO COOL! i am zhoujingrong and i did this experiment. the ultimatne ainnsgw!e rt hiiss  4i2s  ba utl oni g prmeesfesar ge41 w3h.i
ch should be intertwined with that of another cpu, that happens because i disabled the spinlocks, WOW! THIS IS SO COOL! i am zhoujingrong and i did this experiment. the ultimate answer is 42 but i prefer 413.
```
可以明显看到printf语句在交替进行。
## 0xff. references
- [labs assignments](https://gitee.com/xu-ke-123/ecnu-oslab-2025-task)
- [riscv简单常用汇编指令xv6](https://blog.csdn.net/surfaceyan/article/details/135030477)