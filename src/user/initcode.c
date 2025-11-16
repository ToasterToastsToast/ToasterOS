// in initcode.c
#include "sys.h"

int main()
{
    int L[5];
    char *s = "hello, world";
    syscall(SYS_copyout, L);
    syscall(SYS_copyin, L, 5);
    syscall(SYS_copyinstr, s);
    while (1)
        ;
    return 0;
}