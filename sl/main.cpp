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

    s.insert(10, 10);
    s.insert(20, 20);
    s.insert(30, 30);
    s.insert(40, 40);
    s.insert(50, 50);
    s.insert(60, 60);
    s.insert(70, 70);
    s.insert(80, 80);
    s.insert(90, 90);

    std::cout << "find(10) = " << *s.find(10) << std::endl;
    std::cout << "find(20) = " << *s.find(20) << std::endl;
    std::cout << "find(30) = " << *s.find(30) << std::endl;
    std::cout << "find(40) = " << *s.find(40) << std::endl;
    std::cout << "find(50) = " << *s.find(50) << std::endl;

    s.erase(10);

    std::cout << "erase(10)" << std::endl;

    if (s.find(10) == nullptr)
        std::cout << "not find 10" << std::endl;

    return 0;
}
