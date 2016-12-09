#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
    void *buf;
    int   pagesz = getpagesize();
    int   num = 100000;

    if (posix_memalign(&buf, pagesz, pagesz * num))
    {
        printf("cannot allocate\n");
    }

    for (int i = 0; i < num / 2; i++) {
        ((char*)buf)[i * pagesz] = 0;
    }

    // ((char*)buf)[(num - 1) * pagesz] = 0;

    getchar();

    return 0;
}
