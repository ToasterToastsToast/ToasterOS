#include "arch/mod.h"
#include "lib/mod.h"

// static volatile int started = 0;

// int main() {
//     int id = mycpuid();
//     if (id == 0) {

//         printf("┏┳┓┏┓┏┓┏┓┏┳┓┏┓┳┓  ┏┓┏┓\n");
//         printf(" ┃ ┃┃┣┫┗┓ ┃ ┣ ┣┫━━┃┃┗┓\n");
//         printf(" ┻ ┗┛┛┗┗┛ ┻ ┗┛┛┗  ┗┛┗┛\n");
//         printf("hello this is default lucky cpu-0! spin-lock disabled!\n");
//         print_init();

//         started = 1;
//     } else {
//         while (!started)
//             ;
//     }
//     printf("cpu-%d is running!\n", id);

//     return 0;
// }

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

/*
2. 为什么 sum 没有加到 2000000

sum++ 实际上不是原子操作，它可以分解为三步：

读 sum 的当前值到寄存器

寄存器值加 1

写回 sum

假设 CPU0 和 CPU1 同时执行：

CPU0 读到 sum = 100

CPU1 也读到 sum = 100

CPU0 写回 sum = 101

CPU1 写回 sum = 101

这样 sum 只增加了 1，但实际上两个 CPU 都执行了一次 sum++。这种情况被称为 竞争条件 (race condition)。

因为两个 CPU 都有可能在同一时间读写 sum，很多加法操作被覆盖掉了。

所以最后 sum 远小于 2000000。
*/

/*

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
cpu 0 is booting!
cpu 1 is booting!
cpu 0 report: sum = 1978168
cpu 1 report: sum = 2000000

*/

/*
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
        cpu 0 is booting!
cpu 1 is booting!
cpu 0 report: sum = 1000000
cpu 1 report: sum = 2000000
*/