int div_by_zero_const() {
    int a = 10;
    int b = 0;
    int c = a / b;   
    return c;
}

int main() {
    return div_by_zero_const();
}
