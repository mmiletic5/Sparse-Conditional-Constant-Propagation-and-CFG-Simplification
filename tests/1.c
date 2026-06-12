#include <stdio.h>

int test_sccp() {
    int x = 2 + 3;

    if (x == 5) {
        return x + 10;
    } else {
        return x + 20;
    }
}

int test_cfg(int a) {
    int x = a;

    goto middle;

middle:
    goto exit;

dead:
    return 100;

exit:
    return x;
}

int main() {
    return test_sccp();
}