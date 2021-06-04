#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/ioctl.h>

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

struct {
    uint64_t pkts;
    uint64_t bytes;
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
            printf("Saw %ld packets and %ld bytes so far\n", ctr.pkts, ctr.bytes);
        }
        k++;
    }
    quit:
    return;
}

void reporting_thread() {
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

    start_receiving(nmd);
}
