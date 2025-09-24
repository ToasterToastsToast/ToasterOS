#include "arch/mod.h"
#include "lib/mod.h"

int main()
{
    int id = mycpuid();
    printf("cpu %d is booting!\n",id);
    return 0;
}