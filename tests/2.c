#include <stdio.h>

int test_false_branch() {
    int x = 10;

    if (x < 3) {
        return x + 100;
    } else {
        return x - 4;
    }
}

int test_phi_same_constant() {
    int x = 7;
    int y;

    if (x == 7) {
        y = 42;
    } else {
        y = 42;
    }

    return y;
}

int test_unknown_condition(int a) {
    if (a > 0) {
        return a + 1;
    } else {
        return a - 1;
    }
}

int main() {
    int r1 = test_false_branch();
    int r2 = test_phi_same_constant();
    int r3 = test_unknown_condition(5);
    return r1 + r2 + r3;
}
