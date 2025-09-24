# ToasterOS

ECNU Operating System 2025 Fall Final Project 

**Contributors**: ToasterToasterToast, syqwq

--- 

## 0x01. features
- currently none 🥹

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

## 0xff. references
- [labs assignments](https://gitee.com/xu-ke-123/ecnu-oslab-2025-task)