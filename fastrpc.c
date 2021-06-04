#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
#include <locale.h>
#include <pthread.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h> // htons

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


int last_seq_seen = 0;

void proc_pkt(const char *pkt) {
    struct ether_header *ehdr = (struct ether_header*)pkt;
    if (ntohs(ehdr->ether_type) != ETHERTYPE_IP) {
        printf("unexpected ether_type: %d\n", ntohs(ehdr->ether_type));
        return;
    }

    struct ip* iph = (struct ip *)(ehdr + 1);
    if (iph->ip_p != IPPROTO_UDP) {
        printf("got non-udp IP traffic\n");
        return;
    }

    struct udphdr *uhdr = (struct udphdr*)(iph + 1);
    int len = ntohs((int)uhdr->len);

    const char *data = (const char*)(uhdr + 1);

    int seq = *((int*)data);
    printf("src port: %d, dst port: %d, len: %d seq: %d\n", ntohs(uhdr->source), ntohs(uhdr->dest), len, seq);
    // printf("src port: %d, dst port: %d, len: %d seq: %d\n", ntohs(uhdr->source), ntohs(uhdr->dest), len, seq);
    printf("src ip: %s, dst ip: %s\n", inet_ntoa(iph->ip_src), inet_ntoa(iph->ip_dst));
    for(size_t i = 0; i < sizeof ehdr->ether_dhost; i++)
    {
        if(i > 0)
            printf(":%02x", (unsigned int) ehdr->ether_dhost[i] & 0xffu);
        else
            printf("%02x", (unsigned int) ehdr->ether_dhost[i] & 0xffu);
    }
    printf("\n");
    printf("\n");
}

void start_receiving(struct nm_desc *nmd) {
    while (true) {
        // Repeatedly checks the RX ring
        if (ioctl(nmd->fd, NIOCRXSYNC, NULL) < 0) {
            fprintf(stderr, "ioctl error on queue %d: %s", 37,
              strerror(errno));
            goto quit;
        }

        int j;
        for (j = nmd->first_rx_ring; j <= nmd->last_rx_ring; j++) {
            struct netmap_ring *ring = NETMAP_RXRING(nmd->nifp, j);
            if (nm_ring_empty(ring))
                continue;

            int limit = nm_ring_space(ring);
            // printf("%d packets in ring, ", limit);

            unsigned int head;
            int i;
            for (head = ring->head, i = 0; i < limit; i++) {
                struct netmap_slot *slot = &ring->slot[head];
                ctr.bytes += slot->len;
                char *rxbuf = NETMAP_BUF(ring, slot->buf_idx);
                proc_pkt(rxbuf);
                head = nm_ring_next(ring, head);
                ctr.pkts++;
            }

            ring->cur = ring->head = head;
        }

        struct timespec delta;
        delta.tv_sec = 0;
        delta.tv_nsec = 5000; // XXX: 5 us delay before we recheck the RX queue.
        // Even doing 0 delay works, probably because of minimum granularity of
        // sleep
        nanosleep(&delta, NULL);
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
