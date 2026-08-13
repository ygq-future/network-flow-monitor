#include "network_flow_monitor/build/dependencies.h"

#include <cstddef>
#include <iostream>
#include <string_view>

static_assert(sizeof(void*) == 8, "The first release supports x64 only");
static_assert(__cplusplus >= 202002L, "The project requires C++20 or newer");

int main() {
    if (std::string_view{NFM_YYJSON_VERSION} != "0.12.0") {
        std::cerr << "Unexpected yyjson version\n";
        return 1;
    }

    return 0;
}
