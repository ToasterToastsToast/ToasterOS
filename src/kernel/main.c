#include "arch/mod.h"
#include "lib/mod.h"

static volatile int started = 0;

int main() {
    int id = mycpuid();
    if (id == 0) {
        printf("┏┳┓┏┓┏┓┏┓┏┳┓┏┓┳┓  ┏┓┏┓\n");
        printf(" ┃ ┃┃┣┫┗┓ ┃ ┣ ┣┫━━┃┃┗┓\n");
        printf(" ┻ ┗┛┛┗┗┛ ┻ ┗┛┛┗  ┗┛┗┛\n");
        puts("hello this is default lucky cpu-0! spin-lock disabled!");

        print_init();

        started = 1;
    } else {
        while (!started)
            ;
    }
    printf("cpu-%d is running!\n", id);

    return 0;
}