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
