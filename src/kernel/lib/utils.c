#include "mod.h"

// 从begin开始对连续n个字节赋值data
void memset(void *begin, uint8 data, uint32 n)
{
    uint8 *list = (uint8 *)begin;
    for (uint32 i = 0; i < n; i++)
        list[i] = data;
}

// 从src向dst拷贝n个字节的数据
void memmove(void *dst, const void *src, uint32 n)
{
    char *d = dst;
    const char *s = src;
    while (n--)
    {
        *d = *s;
        d++;
        s++;
    }
}

// 字符串p的前n个字符与q做比较
// 按照ASCII码大小逐个比较
// 相同返回0 大于或小于返回正数或负数
int strncmp(const char *p, const char *q, uint32 n)
{
    while (n > 0 && *p && *p == *q)
        n--, p++, q++;
    if (n == 0)
        return 0;
    return (uint8)*p - (uint8)*q;
}

// C 标准库函数返回 dst 的地址。
void *memcpy(void *dst, const void *src, uint32 n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;

    // 如果长度为0，直接返回
    if (n == 0)
    {
        return dst;
    }

    // 标准 memcpy 实现：按字节从头到尾复制
    // 注意：这个实现不处理重叠区域，但 uvm_copyin/out 中使用时，
    // 源地址是 PA，目标地址是内核 VA，通常不会重叠或需要 memmove 的特殊处理。
    while (n--)
    {
        *d = *s;
        d++;
        s++;
    }
    return dst;
}