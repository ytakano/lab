#include "cb.hpp"
#include "cb_ms.hpp"

#include <unistd.h>

#include <iostream>
#include <thread>

cb_ms<uint32_t> q(1024 * 1024);
//cb<uint32_t> q(1024 * 1024);

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

    std::cout << "has RTM?: ";

    if (cpu_has_rtm())
        std::cout << "yes" << std::endl;
    else
        std::cout << "no" << std::endl;

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
