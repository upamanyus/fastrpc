#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
#include <locale.h>
#include <pthread.h>

#define NETMAP_WITH_LIBS
#include "net/netmap_user.h"
#include "libnetmap.h"
#include "net/netmap.h"

struct nm_desc* setup_server(const char *host) {
    struct nm_desc *nmd;
    nmd = nm_open(host, NULL, 0, NULL);
    if (nmd == NULL) {
        if (!errno) {
            printf("Failed to nm_open(%s): not a netmap port\n",
                   host);
        } else {
            printf("Failed to nm_open(%s): %s\n", host,
                   strerror(errno));
        }
        exit(-1);
    }
    return nmd;
}

volatile struct {
    volatile uint64_t pkts;
    volatile uint64_t bytes;
} ctr;

void start_receiving(struct nm_desc *nmd) {
    int k = 0;
    while (true) {
        if (ioctl(nmd->fd, NIOCRXSYNC, NULL) < 0) {
            fprintf(stderr, "ioctl error on queue %d: %s", 37,
              strerror(errno));
            goto quit;
        }

        for (int j = nmd->first_rx_ring; j <= nmd->last_rx_ring; j++) {
            struct netmap_ring *ring = NETMAP_RXRING(nmd->nifp, j);
            if (nm_ring_empty(ring))
                continue;

            int limit = nm_ring_space(ring);

            unsigned int head;
            int i;
            for (head = ring->head, i = 0; i < limit; i++) {
                struct netmap_slot *slot = &ring->slot[head];
                ctr.bytes += slot->len;
                head = nm_ring_next(ring, head);
                ctr.pkts++;
            }

            ring->cur = ring->head = head;
        }
        if (k % 1000000 == 0) {
            // printf("Saw %ld packets and %ld bytes so far\n", ctr.pkts, ctr.bytes);
        }
        k++;
    }
    quit:
    return;
}

void timespec_diff(struct timespec *start, struct timespec *end,
                   struct timespec *result)
{
    if ((end->tv_nsec - start->tv_nsec) < 0) {
        result->tv_sec = end->tv_sec - start->tv_sec - 1;
        result->tv_nsec = end->tv_nsec - start->tv_nsec + 1e9;
    } else {
        result->tv_sec = end->tv_sec - start->tv_sec;
        result->tv_nsec = end->tv_nsec - start->tv_nsec;
    }

    return;
}

void *reporting_thread(void *garbage) {
    garbage = garbage;
    printf("Starting reporting thread\n");
    struct timespec delta, start, end;
    int report_interval = 1000; // in ms

    const int ONE_MIL = 1000000;

    setlocale(LC_NUMERIC, "");
    while (true) {

        delta.tv_sec = report_interval / 1000;
        delta.tv_nsec = (report_interval % 1000) * ONE_MIL;
        clock_gettime(CLOCK_REALTIME, &start);
        int prev_pkts = ctr.pkts;

        nanosleep(&delta, NULL);

        clock_gettime(CLOCK_REALTIME, &end);
        int pkts = ctr.pkts;
        timespec_diff(&start, &end, &delta);
        float pps = (float)(pkts - prev_pkts)/((float)delta.tv_sec + delta.tv_nsec/1e9);
        printf("%'d packets per sec\n", (int)pps);
    }
}

void usage() {
    printf("Bad arguments\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        exit(-1);
    }
    struct nm_desc *nmd = setup_server(argv[1]);

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(&tid, &attr, reporting_thread, NULL);
    start_receiving(nmd);
}
