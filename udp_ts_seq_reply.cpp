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



// Entries are never deleted
std::map<struct sockaddr_in, uint32_t> seqs;
std::map<struct sockaddr_in, struct timespec> lastseens;


bool operator< (const sockaddr_in &a, const sockaddr_in &b) {
    if (a.sin_addr.s_addr != b.sin_addr.s_addr) {
        return a.sin_addr.s_addr < b.sin_addr.s_addr;
    }
    if (a.sin_port != b.sin_port) {
        return a.sin_port < b.sin_port;
    }
    return false;
}

static void serve1(int socket_fd) {
    char buf[65536];
    memset(buf, 0, sizeof(buf));
    
    struct sockaddr_in sa;
    socklen_t sa_len = sizeof(sa);
    
    ssize_t ret = recvfrom(socket_fd, buf, sizeof(buf)-16, 0, (struct sockaddr*)&sa, &sa_len);
    
    if (ret == -1) {
        return;
    }
    
    memmove(buf+16, buf, ret);
    ret+=16;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    if (lastseens.count(sa)) {
        struct timespec lastseen = lastseens[sa];
        struct timespec age;
        timespec_diff(&lastseen, &ts, &age);
        if (age.tv_sec > 60) {
            seqs[sa] = 0;
        }
    } else {
        seqs[sa] = 0;
    }
    
    uint32_t high = htonl((ts.tv_sec&0xFFFFFFFF00000000LL)>>32);
    memcpy(buf+ 0, &high, 4);
    uint32_t low  = htonl((ts.tv_sec&0x00000000FFFFFFFFLL)>>0);
    memcpy(buf+ 4, &low, 4);
    uint32_t nano = htonl(ts.tv_nsec);
    memcpy(buf+ 8, &nano, 4);
    uint32_t seq = htonl(seqs[sa]);
    memcpy(buf+ 12, &seq, 4);
    
    sendto(socket_fd, buf, ret, 0,  (struct sockaddr*)&sa, sa_len);
    
    ++seqs[sa];
    lastseens[sa] = ts;
}

static void probe(int s, int pktsize, int kbps, int overhead) {
    long delay_ns = (pktsize+overhead)*8000000 / kbps;
    
    uint8_t buf[65536];
    
    memset(buf, 0, pktsize);
    
    uint32_t seq_ = 0;
    struct timespec ts;
    struct timespec ts_next;
    
    clock_gettime(CLOCK_MONOTONIC, &ts_next);
    for(;;) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        
        uint32_t high = htonl((ts.tv_sec&0xFFFFFFFF00000000LL)>>32);
        memcpy(buf+ 0, &high, 4);
        uint32_t low  = htonl((ts.tv_sec&0x00000000FFFFFFFFLL)>>0);
        memcpy(buf+ 4, &low, 4);
        uint32_t nano = htonl(ts.tv_nsec);
        memcpy(buf+ 8, &nano, 4);
        uint32_t seq = htonl(seq_);
        memcpy(buf+ 12, &seq, 4);
        
        send(s, buf, pktsize, 0);
        
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
};

static void measure(int s) {
    char buf[65536];
    
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    bool firstreport = true;
    struct snapshot info;
    uint32_t seq3 = 0;
    struct timespec basets2;
    uint32_t minrtt = 0xFFFFFFFF;
    
    for(;;) {
        ssize_t ret = recv(s, buf, sizeof(buf), 0);
        if (ret == -1) {
            if (errno == EAGAIN) {
                //fputc('-', stderr);
            } else {
                fputc('!', stderr);
            }
            continue;
        } else {
            //fputc('.', stderr);
        }
        
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
        }
        {
            uint32_t high;
            uint32_t low;
            uint32_t nano;
            uint32_t seq;
            memcpy( &high    ,buf+ 16+0  , 4);
            memcpy( &low     ,buf+ 16+4  , 4);
            memcpy( &nano    ,buf+ 16+8  , 4);
            memcpy( &seq     ,buf+ 16+12 , 4);
            info.ts1.tv_sec = ((uint64_t)ntohl(high)) << 32;
            info.ts1.tv_sec |= ntohl(low);
            info.ts1.tv_nsec = ntohl(nano);
            info.seq1 = ntohl(seq);
        }
        
        if(false){
            fprintf(stderr, "ts1=%lu %lu  ts2=%lu %lu ts3=%lu %lu\n",
                    (unsigned long) info.ts1.tv_sec, (unsigned long) info.ts1.tv_nsec,
                    (unsigned long) info.ts2.tv_sec, (unsigned long) info.ts2.tv_nsec,
                    (unsigned long) info.ts3.tv_sec, (unsigned long) info.ts3.tv_nsec);
        }
        info.seq3 = seq3;
        if (firstreport) {
            firstreport = false;
            basets2 = info.ts2;
        }
        
        struct timespec t;
        timespec_diff(&start, &info.ts3, &t);
        
        long cumseqloss_loc = (long)info.seq1 - info.seq3;
        long cumseqloss_rem = (long)info.seq2 - info.seq3;
        
        struct timespec rtt_;
        timespec_diff(&info.ts1, &info.ts3, &rtt_);
        long rtt = timespec_milli(&rtt_);
        if (minrtt > rtt) minrtt = rtt;
        
        struct timespec remote;
        struct timespec remote_d_;
        timespec_diff(&basets2, &info.ts2, &remote);
        timespec_diff(&remote, &t, &remote_d_);
        long remote_d = timespec_milli(&remote_d_);
        
        fprintf(stdout, "%ld ", timespec_milli(&t));
        fprintf(stdout, "%ld ", cumseqloss_rem);
        fprintf(stdout, "%ld ", minrtt/2 + rtt - remote_d);
        fprintf(stdout, "%ld ", cumseqloss_loc-cumseqloss_rem);
        fprintf(stdout, "%ld ", remote_d - minrtt/2);
        
        fprintf(stdout, "\n");
        
        seq3 += 1;
    }
}


int main(int argc, char* argv[]) {
    if (argc != 4 && argc != 7) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "    udp_ts_seq_reply serve listen_addr listen_port \n");
        fprintf(stderr, "    udp_ts_seq_reply probe connect_addr connect_port packet_size kbps overhead_bytes_per_packet\n");
        fprintf(stderr, "'serve' echoes UDP packets back, prepending ts and seq num.\n");
        fprintf(stderr, "'probe' measures network delay and loss.\n");
        fprintf(stderr, "Example:\n");
        fprintf(stderr, "    udp_ts_seq_reply serve 0.0.0.0 919\n");
        fprintf(stderr, "    udp_ts_seq_reply probe 0.0.0.0 919  140 30 32\n");
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
        if (connect(s, addr->ai_addr, addr->ai_addrlen)) {  perror("connect");  return 5;  }
        
        
        int packet_size = atoi(argv[4]);
        int kbps            = atoi(argv[5]);
        int overhead         = atoi(argv[6]);
        
        if (packet_size < 16) {
            fprintf(stderr, "Minimal packet_size is 16\n");
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
            fprintf(stderr, "Caution: kbps > 100mbit/s. Do you really want to flood the network?\n");
            fprintf(stderr, "(waiting for 3 seconds before continuing)");
            sleep(3);
        }
        
        pid_t f = fork();
        if(!f) {
            probe(s, packet_size, kbps, overhead);
        } else 
        if (f > 0) {
            measure(s);
        } else {
            perror("fork");
        }
    }
}

