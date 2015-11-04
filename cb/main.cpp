#include "cb.hpp"
#include "cb_ms.hpp"
#include "cb_ms_spin.hpp"

#include <unistd.h>

#include <iostream>
#include <thread>

cb_ms_spin<uint64_t> q(1024 * 1024);
//cb_ms<uint64_t> q(1024 * 1024);
//cb<uint64_t> q(1024 * 1024);

uint64_t val = 0;

void
producer()
{
    uint64_t n = 0;

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
//    std::thread th2(producer), th3(producer);

    std::cout << "has RTM?: ";

    if (cpu_has_rtm())
        std::cout << "yes" << std::endl;
    else
        std::cout << "no" << std::endl;

    uint64_t v = val;
    for (;;) {
        std::cout << "val = " << val
                  << " (" << (val - v) / 5 << " ops/s)"
                  << "\nlen = " << q.get_len() << std::endl;
        v = val;
        sleep(5);
    }

    return 0;
}
