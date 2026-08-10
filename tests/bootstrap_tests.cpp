#include <cstddef>

static_assert(sizeof(void*) == 8, "The first release supports x64 only");
static_assert(__cplusplus >= 202002L, "The project requires C++20 or newer");

int main() {
    return 0;
}
