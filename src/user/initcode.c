// in initcode.c
#include "sys.h"

#define PGSIZE 4096
// int test1()
// {
//     int L[5];
//     char *s = "hello, world";
//     syscall(SYS_copyout, L);
//     syscall(SYS_copyin, L, 5);
//     syscall(SYS_copyinstr, s);
//     while (1)
//         ;
//     return 0;
// }




int main()
{

    long long heap_top = 0;

    heap_top = syscall(SYS_brk, 0);
    heap_top = syscall(SYS_brk, heap_top + PGSIZE * 9);
    heap_top = syscall(SYS_brk, heap_top);
    heap_top = syscall(SYS_brk, heap_top - PGSIZE * 5);

    while (1)
        ;
    return 0;
}