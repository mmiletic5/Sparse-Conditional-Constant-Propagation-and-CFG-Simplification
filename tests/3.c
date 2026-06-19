#include <stdio.h>

int test_chain_elimination() {
    int a = 6;
    int b = 4;
    int diff = a - b;
    int total = diff * 5;

    if (total > 8) {
        if (total < 15) {
            if (diff == 2) {
                return total;
            } else {
                return -3;
            }
        } else {
            return -2;
        }
    } else {
        return -1;
    }
}

int test_dead_code_after_fold() {
    int debug_mode = 0;

    if (debug_mode) {
        return -1;
    }

    int x = 10;
    int y = x * x;
    int result = y - x;

    return result;
}

int main() {
    int r1 = test_chain_elimination();
    int r2 = test_dead_code_after_fold();
    return r1 + r2;
}
