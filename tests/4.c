#include <stdio.h>

int test_phi_then_cfg() {
    int flag = 1;
    int val;

    if (flag > 0) {
        val = 100;
    } else {
        val = 200;
    }

    int result = val * 3;

    if (val == 100) {
        return result;
    } else {
        return 0;
    }
}

int test_mixed_known_unknown(int n) {
    int base = 50;
    int limit = base + 50;

    if (base < 0) {
        return -1;
    }

    if (n > limit) {
        return n + base;
    } else {
        return limit - base;
    }
}

int main() {
    int r1 = test_phi_then_cfg();
    int r2 = test_mixed_known_unknown(200);
    return r1 + r2;
}
