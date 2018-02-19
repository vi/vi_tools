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


// Entries are never deleted
std::map<struct sockaddr_in, uint32_t> seqs;


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
    
    ++seqs[sa];
    
    uint32_t high = htonl((ts.tv_sec&0xFFFFFFFF00000000LL)>>32);
    memcpy(buf+ 0, &high, 4);
    uint32_t low  = htonl((ts.tv_sec&0x00000000FFFFFFFFLL)>>0);
    memcpy(buf+ 4, &low, 4);
    uint32_t nano = htonl(ts.tv_nsec);
    memcpy(buf+ 8, &nano, 4);
    uint32_t seq = htonl(seqs[sa]);
    memcpy(buf+ 12, &seq, 4);
    
    sendto(socket_fd, buf, ret, 0,  (struct sockaddr*)&sa, sa_len);
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

static void measure(int s, int report_interval_ms) {
    struct timeval tv;
    tv.tv_sec = report_interval_ms / 1000;
    tv.tv_usec = report_interval_ms % 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    char buf[65536];
    
    for(;;) {
        ssize_t ret = recv(s, buf, sizeof(buf), 0);
        if (ret == -1) {
            if (errno == EAGAIN) {
                fputc('-', stderr);
            } else {
                fputc('!', stderr);
            }
        } else {
            fputc('.', stderr);
        }
    }
}


int main(int argc, char* argv[]) {
    if (argc != 4 && argc != 8) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "    udp_ts_seq_reply serve listen_addr listen_port \n");
        fprintf(stderr, "    udp_ts_seq_reply probe connect_addr connect_port packet_size kbps overhead_bytes_per_packet report_interval_ms\n");
        fprintf(stderr, "'serve' echoes UDP packets back, prepending ts and seq num.\n");
        fprintf(stderr, "'probe' measures network delay and loss.\n");
        fprintf(stderr, "Example:\n");
        fprintf(stderr, "    udp_ts_seq_reply serve 0.0.0.0 919\n");
        fprintf(stderr, "    udp_ts_seq_reply probe 0.0.0.0 919  140 30 32 500\n");
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
        if (argc != 8) {
            fprintf(stderr, "Command 'probe' must have 6 additional params\n");
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
        int report_interval_ms = atoi(argv[7]);
        
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
        if ( ((packet_size + overhead) * 8000 / kbps) > 1000000 ) {
            fprintf(stderr, "Packets would be sent too rarely\n");
            // and timeout calculations don't handle overflows
            return 1;
        }
        if (kbps > 100*1000) {
            fprintf(stderr, "Caution: kbps > 100mbit/s. Do you really want to flood the network?\n");
            fprintf(stderr, "(waiting for 3 seconds before continue)");
            sleep(3);
        }
        
        pid_t f = fork();
        if(!f) {
            probe(s, packet_size, kbps, overhead);
        } else 
        if (f > 0) {
            measure(s, report_interval_ms);
        } else {
            perror("fork");
        }
    }
}

