#include "sl.hpp"

#include <time.h>

#include <iostream>

int
main(int argc, char *argv[])
{
    srand(time(NULL));

    sl<int, int> s;
    const int *p;

    s.erase(10);

    for (int i = 0; i < 10000; i++) {
        s.insert(i, i);
    }

    std::cout << "find(10) = " << *s.find(10) << std::endl;
    std::cout << "find(20) = " << *s.find(20) << std::endl;
    std::cout << "find(30) = " << *s.find(30) << std::endl;
    std::cout << "find(40) = " << *s.find(40) << std::endl;
    std::cout << "find(50) = " << *s.find(50) << std::endl;

    s.erase(10);

    std::cout << "erase(10)" << std::endl;

    if (s.find(10) == nullptr)
        std::cout << "not find 10" << std::endl;

    for (int i = 0; i < 10000; i++) {
        s.erase(i);
        if (s.find(i) != nullptr)
            std::cout << "ERROR: i = " << i << std::endl;
    }

    if (s.find(255) == nullptr)
        std::cout << "not find 255" << std::endl;

    return 0;
}
