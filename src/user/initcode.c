// // in initcode.c
// #include "sys.h"

// // 与内核保持一致
// #define VA_MAX       (1ul << 38)
// #define PGSIZE       4096
// #define MMAP_END     (VA_MAX - (16 * 256 + 2) * PGSIZE)
// #define MMAP_BEGIN   (MMAP_END - 64 * 256 * PGSIZE)

// int main()
// {
//     // 建议画图理解这些地址和长度的含义

//     // sys_mmap 测试 
//     syscall(SYS_mmap, MMAP_BEGIN + 4 * PGSIZE, 3 * PGSIZE);
//     syscall(SYS_mmap, MMAP_BEGIN + 10 * PGSIZE, 2 * PGSIZE);
//     syscall(SYS_mmap, MMAP_BEGIN + 2 * PGSIZE,  2 * PGSIZE);
//     syscall(SYS_mmap, MMAP_BEGIN + 12 * PGSIZE, 1 * PGSIZE);
//     syscall(SYS_mmap, MMAP_BEGIN + 7 * PGSIZE, 3 * PGSIZE);
//     syscall(SYS_mmap, MMAP_BEGIN, 2 * PGSIZE);
//     syscall(SYS_mmap, 0, 10 * PGSIZE);

//     // sys_munmap 测试
//     syscall(SYS_munmap, MMAP_BEGIN + 10 * PGSIZE, 5 * PGSIZE);
//     syscall(SYS_munmap, MMAP_BEGIN, 10 * PGSIZE);
//     syscall(SYS_munmap, MMAP_BEGIN + 17 * PGSIZE, 2 * PGSIZE);
//     syscall(SYS_munmap, MMAP_BEGIN + 15 * PGSIZE, 2 * PGSIZE);
//     syscall(SYS_munmap, MMAP_BEGIN + 19 * PGSIZE, 2 * PGSIZE);
//     syscall(SYS_munmap, MMAP_BEGIN + 22 * PGSIZE, 1 * PGSIZE);
//     syscall(SYS_munmap, MMAP_BEGIN + 21 * PGSIZE, 1 * PGSIZE);

//     while(1);
//     return 0;
// }

// in initcode.c
// #include "sys.h"

// int main()
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
// in initcode.c
#include "sys.h"

#define PGSIZE 4096

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