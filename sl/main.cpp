#include "sl.hpp"

#include <time.h>

#include <iostream>

int
main(int argc, char *argv[])
{
    srand(time(NULL));

    sl<int, int> s;

    return 0;
}
