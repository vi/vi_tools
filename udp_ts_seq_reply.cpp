#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <signal.h>

#include <map>


void timespec_diff(const struct timespec *start, const struct timespec *stop,
                   struct timespec *result)
{
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        result->tv_sec = stop->tv_sec - start->tv_sec - 1;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        result->tv_sec = stop->tv_sec - start->tv_sec;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec;
    }

    return;
}

long int timespec_milli(const struct timespec* q) {
    return 1000*q->tv_sec + q->tv_nsec / 1000 / 1000;
}

#define IP_MASK "\x44\x55\x66\x77"
#define PORT_MASK "\x88\x99"

#define NEWPROTO_SIGNATURE "nUTs"


// Entries are never deleted
std::map<uint32_t, uint32_t> seqs;
std::map<uint32_t, struct timespec> lastseens;

static void serve1(int socket_fd) {
    char buf[65536];
    memset(buf, 0, sizeof(buf));
    
    struct sockaddr_in sa;
    socklen_t sa_len = sizeof(sa);
    
    ssize_t ret = recvfrom(socket_fd, buf, sizeof(buf)-22, 0, (struct sockaddr*)&sa, &sa_len);
    
    if (ret == -1) {
        return;
    }
    
    uint32_t id = 0;

    bool also_put_ip_port = false;
    
    if (ret >= 4) {
        memcpy(&id, buf, 4);
    }

    if (ret >= 8) {
        if (!memcmp(buf+4,NEWPROTO_SIGNATURE,4)) also_put_ip_port = true;
    }
    
    if (!also_put_ip_port) {
        memmove(buf+16, buf, ret);
        ret+=16;
    } else {
        memmove(buf+22, buf, ret);
        ret+=22;
    }
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    if (lastseens.count(id)) {
        struct timespec lastseen = lastseens[id];
        struct timespec age;
        timespec_diff(&lastseen, &ts, &age);
        if (age.tv_sec > 60) {
            seqs[id] = 0;
        }
    } else {
        seqs[id] = 0;
    }
    
    
    uint32_t high = htonl((ts.tv_sec&0xFFFFFFFF00000000LL)>>32);
    memcpy(buf+ 0, &high, 4);
    uint32_t low  = htonl((ts.tv_sec&0x00000000FFFFFFFFLL)>>0);
    memcpy(buf+ 4, &low, 4);
    uint32_t nano = htonl(ts.tv_nsec);
    memcpy(buf+ 8, &nano, 4);
    uint32_t seq = htonl(seqs[id]);
    memcpy(buf+ 12, &seq, 4);
    
    if (also_put_ip_port) {
        if (sa.sin_family == AF_INET) {
            size_t i; 
            uint8_t *b = (uint8_t*)buf + 16;
            uint8_t *a = (uint8_t*)&sa.sin_addr;
            for(i=0; i<4; ++i) {
                b[i] = a[i] ^ IP_MASK[i];
            }
            b = (uint8_t*)buf + 20;
            a = (uint8_t*)&sa.sin_port;
            for(i=0; i<2; ++i) {
                b[i] = a[i] ^ PORT_MASK[i];
            }
        } else {
            memcpy(buf+16, IP_MASK, 4);
            memcpy(buf+20, PORT_MASK, 2);
        }
    }
    
    sendto(socket_fd, buf, ret, 0,  (struct sockaddr*)&sa, sa_len);
    
    ++seqs[id];
    lastseens[id] = ts;
}

struct timespec start;

pid_t probe_pid;

static void probe(int s, int pktsize, int kbps, int overhead, struct addrinfo* to) {
    long delay_ns = ((pktsize+overhead) * 8000) / kbps *1000;
    
    uint8_t buf[65536];
    
    uint32_t id;
    
    memset(buf, 0, pktsize);
    
    unsigned packets_in_first_5_seconds = 1000*1000*1000 / delay_ns * 5;
    
    uint32_t seq_ = 0;
    struct timespec ts;
    struct timespec ts_next;
    
    clock_gettime(CLOCK_MONOTONIC, &ts_next);
    id = (ts_next.tv_sec & 0xFFFFFFFF) ^ (ts_next.tv_nsec & 0xFFFFFFFF);
    for(;;) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        
        memcpy(buf+ 0, &id, 4);
        uint32_t high = htonl((ts.tv_sec&0xFFFFFFFF00000000LL)>>32);
        memcpy(buf+ 4, NEWPROTO_SIGNATURE, 4); // new protocol version. Like "STUN", but not actual STUN.
        memcpy(buf+ 8, &high, 4);
        uint32_t low  = htonl((ts.tv_sec&0x00000000FFFFFFFFLL)>>0);
        memcpy(buf+ 12, &low, 4);
        uint32_t nano = htonl(ts.tv_nsec);
        memcpy(buf+ 16, &nano, 4);
        uint32_t seq = htonl(seq_);
        memcpy(buf+ 20, &seq, 4);
        
        int len = pktsize;
        
        if (seq_ < packets_in_first_5_seconds) {
            len = 24; // warmup to get min rtt.
        }
        
        sendto(s, buf, len, 0, to->ai_addr, to->ai_addrlen);
        
        seq_+=1;
        
        ts_next.tv_nsec += delay_ns;
        if (ts_next.tv_nsec > 1000*1000*1000) {
            ts_next.tv_nsec -= 1000*1000*1000;
            ts_next.tv_sec += 1;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts_next, NULL);
    }
}

struct snapshot {
    struct timespec ts1;
    struct timespec ts2;
    struct timespec ts3;
    uint32_t seq1;
    uint32_t seq2;
    uint32_t seq3;
    unsigned char ip[4];
    unsigned short port;
};

static void measure(int s) {
    char buf[65536];
    
    bool firstreport = true;
    bool outage_reported = false;
    struct snapshot info;
    uint32_t seq3 = 0;
    struct timespec basets2 = {0,0};
    uint32_t minrtt = 0xFFFFFFFF;
    
    clock_gettime(CLOCK_REALTIME, &info.ts3);
    printf("CLOCK_REALTIME=%ld.%09ld ", 
                (long)info.ts3.tv_sec, 
                (long)info.ts3.tv_nsec);
    clock_gettime(CLOCK_MONOTONIC, &info.ts3);
    printf("CLOCK_MONOTONIC=%ld.%09ld\n", 
                (long)info.ts3.tv_sec, 
                (long)info.ts3.tv_nsec);
    
    for(;;) {
        if (!firstreport) {
            struct timespec check;
            clock_gettime(CLOCK_MONOTONIC, &check);
            struct timespec diff;
            timespec_diff(&info.ts3, &check, &diff);
            long disc = timespec_milli(&diff);
            if (disc > 3) {
                printf(" Output was clogged for %ld ms\n", disc);
            }
        }
        
        struct sockaddr_storage sas;
        socklen_t sas_len = sizeof(sas);

        ssize_t ret = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr*)&sas, &sas_len);
        if (ret == -1) {
            if (errno == EAGAIN) {
                if (!outage_reported) {
                    printf("OUTAGE\n");
                    outage_reported = true;
                }
            } else {
                printf("ERROR\n");
            }
            clock_gettime(CLOCK_MONOTONIC, &info.ts3);
            fflush(stdout);
            continue;
        } else {
            //fputc('.', stderr);
        }
        
        
        if (firstreport) {
            firstreport = false;
        }
        outage_reported = false;
        
        uint32_t id;
        
        clock_gettime(CLOCK_MONOTONIC, &info.ts3);
        {
            uint32_t high;
            uint32_t low;
            uint32_t nano;
            uint32_t seq;
            memcpy( &high    ,buf+ 0  , 4);
            memcpy( &low     ,buf+ 4  , 4);
            memcpy( &nano    ,buf+ 8  , 4);
            memcpy( &seq     ,buf+ 12 , 4);
            info.ts2.tv_sec = ((uint64_t)ntohl(high)) << 32;
            info.ts2.tv_sec |= ntohl(low);
            info.ts2.tv_nsec = ntohl(nano);
            info.seq2 = ntohl(seq);

            info.ip[0] = buf[16] ^ IP_MASK[0];
            info.ip[1] = buf[17] ^ IP_MASK[1];
            info.ip[2] = buf[18] ^ IP_MASK[2];
            info.ip[3] = buf[19] ^ IP_MASK[3];
            info.port = ((unsigned short)(buf[20] ^ PORT_MASK[0]) << 8) + (unsigned short)(buf[21] ^ PORT_MASK[1]);
        }
        {
            uint32_t high;
            uint32_t low;
            uint32_t nano;
            uint32_t seq;
            memcpy( &id      ,buf+ 22+0  , 4);
            memcpy( &high    ,buf+ 22+8  , 4);
            memcpy( &low     ,buf+ 22+12 , 4);
            memcpy( &nano    ,buf+ 22+16 , 4);
            memcpy( &seq     ,buf+ 22+20 , 4);
            info.ts1.tv_sec = ((uint64_t)ntohl(high)) << 32;
            info.ts1.tv_sec |= ntohl(low);
            info.ts1.tv_nsec = ntohl(nano);
            info.seq1 = ntohl(seq);
        }
        
        info.seq3 = seq3;
        
        struct timespec t;
        timespec_diff(&start, &info.ts1, &t);
        
        long cumseqloss_loc = (long)info.seq1 - info.seq3;
        long cumseqloss_rem = (long)info.seq2 - info.seq3;
        
        struct timespec rtt_;
        timespec_diff(&info.ts1, &info.ts3, &rtt_);
        long rtt = timespec_milli(&rtt_);
        
        if (minrtt > rtt) {
            minrtt = rtt;
            
            //fprintf(stdout, "basets2 debug: ");
            struct timespec d;
            timespec_diff(&info.ts1, &info.ts2, &d);
            
            //fprintf(stdout, "%lu.%03lu + %lu.%03lu = ",
            //    (unsigned long) start.tv_sec, (unsigned long) start.tv_nsec/1000/1000,
            //    (unsigned long) d.tv_sec, (unsigned long) d.tv_nsec/1000/1000);
            
            basets2 = start;
            basets2.tv_sec += d.tv_sec;
            basets2.tv_nsec += d.tv_nsec;
            if (basets2.tv_nsec > 1000*1000*1000) {
                basets2.tv_sec+=1;
                basets2.tv_nsec-=1000*1000*1000;
            }
            //fprintf(stdout, "%lu.%03lu\n",
            //    (unsigned long) basets2.tv_sec, (unsigned long) basets2.tv_nsec/1000/1000);
            
        }
        
        struct timespec remote;
        struct timespec remote_d_;
        timespec_diff(&basets2, &info.ts2, &remote);
        timespec_diff(&t, &remote, &remote_d_);
        long remote_d = timespec_milli(&remote_d_);
        
        fprintf(stdout, "%5ld ", timespec_milli(&t));
        fprintf(stdout, "%3ld ", cumseqloss_loc-cumseqloss_rem);
        fprintf(stdout, "%4ld ", remote_d + minrtt/2);
        fprintf(stdout, "%3ld ", cumseqloss_rem);
        fprintf(stdout, "%4ld ", rtt - remote_d - minrtt/2);
        
        fprintf(stdout, "  ");
        
        fprintf(stdout, "%lu %lu %lu  ",
            (unsigned long) info.seq1,
            (unsigned long) info.seq2,
            (unsigned long) info.seq3);
        fprintf(stdout, "%lu.%03lu %lu.%03lu %lu.%03lu  ",
            (unsigned long) info.ts1.tv_sec, (unsigned long) info.ts1.tv_nsec/1000/1000,
            (unsigned long) info.ts2.tv_sec, (unsigned long) info.ts2.tv_nsec/1000/1000,
            (unsigned long) info.ts3.tv_sec, (unsigned long) info.ts3.tv_nsec/1000/1000);
        
        fprintf(stdout, "%02d %lu", (int)ret, (unsigned long)id);
        
        fprintf(stdout, "\n");
        fflush(stdout);
        
        seq3 += 1;
    }
}

static void signal_handler(int x) {
    if (probe_pid > 0) {
        kill(probe_pid, SIGTERM);
    }
    _exit(0);
}

int main(int argc, char* argv[]) {
    if (argc != 4 && argc != 7) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "    udp_ts_seq_reply serve listen_addr listen_port \n");
        fprintf(stderr, "    udp_ts_seq_reply probe connect_addr connect_port kbps packet_size overhead_bytes_per_packet\n");
        fprintf(stderr, "'serve' echoes UDP packets back, prepending ts and seq num.\n");
        fprintf(stderr, "'probe' measures network delay and loss.\n");
        fprintf(stderr, "Example:\n");
        fprintf(stderr, "    udp_ts_seq_reply serve 0.0.0.0 919\n");
        fprintf(stderr, "    udp_ts_seq_reply probe 0.0.0.0 919  30  140 32\n");
        fprintf(stderr, "\n");
        return 1;
    }
    
    const char* cmd = argv[1];
    const char* addr_ = argv[2];
    const char* port_ = argv[3];
    
    if (!!strcmp(cmd, "serve") && !!strcmp(cmd, "probe")) {
        fprintf(stderr, "Command must be 'serve' or 'probe'\n");
        return 1;
    }
    if (!strcmp(cmd, "serve")) {
        if (argc != 4) {
            fprintf(stderr, "Command 'serve' must have 2 additional params\n");
        }
    }
    if (!strcmp(cmd, "probe")) {
        if (argc != 7) {
            fprintf(stderr, "Command 'probe' must have 5 additional params\n");
        }
    }
    
    struct addrinfo hints;
    struct addrinfo *addr;
    
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_family = AF_INET;
    if (!strcmp(cmd, "serve")) {
        hints.ai_flags = AI_PASSIVE;
    }
    
    int gai_error;
    gai_error=getaddrinfo(addr_,port_, &hints, &addr);
    if (gai_error) { fprintf(stderr, "getaddrinfo 1: %s\n",gai_strerror(gai_error)); return 4; }
    if (!addr) { fprintf(stderr, "getaddrinfo returned no addresses\n");   return 6;  }
    if (addr->ai_next) {
        fprintf(stderr, "Warning: using only one of addresses retuned by getaddrinfo\n");
    }

    struct addrinfo *probe_bind_addr = NULL;
    
    
    int s;
    s = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (s == -1) { perror("socket"); return 7; }
    if (!strcmp(cmd, "serve")) {
        {
            int one = 1;
            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        }
        if (bind(s, addr->ai_addr, addr->ai_addrlen)) {  perror("bind");  return 5;  }
        
        for(;;) {
            serve1(s);
            //usleep(250*1000);
        }
    }
    if (!strcmp(cmd, "probe")) {
        //if (connect(s, addr->ai_addr, addr->ai_addrlen)) {  perror("connect");  return 5;  }
        hints.ai_flags = AI_PASSIVE;
        gai_error=getaddrinfo("0.0.0.0","0", &hints, &probe_bind_addr);
        if (gai_error) { fprintf(stderr, "getaddrinfo 2: %s\n",gai_strerror(gai_error)); return 4; }
        if (!probe_bind_addr) { fprintf(stderr, "getaddrinfo 2 returned no addresses\n");   return 6;  }
        if (bind(s, probe_bind_addr->ai_addr, probe_bind_addr->ai_addrlen)) { perror("bind"); return 5; }
        
        int kbps           = atoi(argv[4]);
        int packet_size    = atoi(argv[5]);
        int overhead       = atoi(argv[6]);
        
        if (packet_size < 24) {
            fprintf(stderr, "Minimal packet_size is 24\n");
            return 1;
        }
        if (packet_size > 65536) {
            fprintf(stderr, "Maximla packet_size is 65536\n");
            return 1;
        }
        if (kbps < 1) {
            fprintf(stderr, "kbps too low");
            return 1;
        }
        int delay_us = (packet_size + overhead) * 8000 / kbps;
        if ( delay_us > 1000000 ) {
            fprintf(stderr, "Packets would be sent too rarely\n");
            // and timeout calculations don't handle overflows
            return 1;
        }
        if (kbps > 100*1000) {
            fprintf(stderr, "Caution: kbps > 100mbit/s.\n");
            sleep(1);
            fprintf(stderr, "Do you really want to flood the network?\n");
            sleep(1);
            fprintf(stderr, "(waiting for 3 seconds before continuing)");
            sleep(3);
        }

        printf("SETTINGS %s %s %d %d %d\n",
               addr_, port_, kbps, packet_size, overhead);
        
        clock_gettime(CLOCK_MONOTONIC, &start);
        probe_pid = fork();
        if(!probe_pid) {
            probe(s, packet_size, kbps, overhead, addr);
        } else 
        if (probe_pid > 0) {
            {
                struct sigaction sa;
                memset(&sa, 0, sizeof(sa));
                sa.sa_handler = &signal_handler;
                sigaction(SIGINT, &sa, NULL);
                sigaction(SIGTERM, &sa, NULL);
                sigaction(SIGPIPE, &sa, NULL);
            }
            
            {
                long delay_us = ((packet_size+overhead) * 8000) / kbps * 1;
                delay_us *= 10;
                if (delay_us < 200*1000) {
                    delay_us = 200*1000;
                }
                struct timeval tv;
                tv.tv_sec = delay_us / 1000 / 1000;
                tv.tv_usec = delay_us % (1000 * 1000);
                setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            }
        
            measure(s);
        } else {
            perror("fork");
        }
    }
}

