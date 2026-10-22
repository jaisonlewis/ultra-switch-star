#include <cstdio>
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    printf("hi\n");
    return 0;
}
