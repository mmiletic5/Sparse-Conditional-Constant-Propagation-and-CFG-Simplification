int diamond_diff_value(int flag) {
    int y;
    if (flag) {
        y = 10;
    } else {
        y = 99;
    }
    int z = y + 1;   
    return z;
}

int main() {
    return diamond_diff_value(1);
}
