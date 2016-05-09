#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int
main(int argc, char **argv)
{

    int kq;

    if ((kq = kqueue()) == -1) {
        perror("kqueue()");
        exit(-1);
    }

    struct kevent kev[2], change[2];

    EV_SET(&kev[0], STDIN_FILENO, EVFILT_READ, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0, 0);
    EV_SET(&kev[1], -1, EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 5000, 0);

    kevent(kq, kev, 2, NULL, 0, NULL);

    for (;;) {
        int nev = kevent(kq, NULL, 0, change, 2, NULL);
        if (nev < 0) {
            perror("kevent()");
            exit(-1);
        }

        for (int i = 0; i < nev; i++) {
            if (change[i].flags & EV_ERROR) {   /* report any error */
                fprintf(stderr, "EV_ERROR: %s\n", strerror(change[i].data));
                exit(-1);
            }

            if (change[i].ident == STDIN_FILENO) {
                char *buf = malloc(change[i].data + 1);
                int n = read(STDIN_FILENO, buf, change[i].data);

                buf[change[i].data - 1] = '\0';
                printf("stdin!: buf = %s\n", buf);
                free(buf);
            } else if (change[i].ident == -1) {
                printf("timeout!\n");
            } else {
                fprintf(stderr, "not reach here\n");
                exit(-1);
            }
        }
    }
    
    return 0;
}
