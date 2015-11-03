#include "cb.hpp"

#include <unistd.h>

#include <iostream>
#include <thread>

cb<uint32_t, 1024 * 1024> q;
uint32_t val = 0;

void
producer()
{
    uint32_t n = 0;

    for (;;) {
        q.push(n++);
    }
}

void
consumer()
{
    for (;;) {
        val = q.pop();
    }
}

int
main(int argc, char *argv[])
{
    std::thread th0(producer), th1(consumer);

    uint32_t v = val;
    for (;;) {
        std::cout << "val = " << val
                  << " (" << (val - v) / 5 << " ops/s)"
                  << "\nlen = " << q.get_len() << std::endl;
        v = val;
        sleep(5);
    }

    return 0;
}
