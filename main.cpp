// main.cpp
#include <iostream>
#include <ff/ff.hpp>
#include <ff/parallel_for.hpp>

auto main() -> int {
    ff::ParallelFor pf(4); // Use 4 threads
    pf.parallel_for(0, 10, 1, [](const long I) {
        std::cout << "Hello from task " << I << std::endl;
    });
    return 0;
}
