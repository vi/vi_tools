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

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "    udp_ts_seq_reply listen_addr listen_port \n");
        fprintf(stderr, "Echoes packets back, prepending ts and seq num.\n");
        fprintf(stderr, "Example:\n");
        fprintf(stderr, "    udp_ts_seq_reply serve 0.0.0.0 919\n");
        fprintf(stderr, "\n");
        return 1;
    }
    
    const char* bind_addr_ = argv[1];
    const char* bind_port_ = argv[2];
    
    struct addrinfo hints;
    struct addrinfo *recv_addr;
    
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_family = AF_INET;
    hints.ai_flags = AI_PASSIVE;
    
    int gai_error;
    gai_error=getaddrinfo(bind_addr_,bind_port_, &hints, &recv_addr);
    if (gai_error) { fprintf(stderr, "getaddrinfo 1: %s\n",gai_strerror(gai_error)); return 4; }
    if (!recv_addr) { fprintf(stderr, "getaddrinfo returned no addresses\n");   return 6;  }
    if (recv_addr->ai_next) {
        fprintf(stderr, "Warning: using only one of addresses retuned by getaddrinfo\n");
    }
    
    
    int s;
    s = socket(recv_addr->ai_family, recv_addr->ai_socktype, recv_addr->ai_protocol);
    if (s == -1) { perror("socket"); return 7; }
    {
        int one = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    }
    if (bind(s, recv_addr->ai_addr, recv_addr->ai_addrlen)) {  perror("bind");  return 5;  }
    
    for(;;) {
        serve1(s);
        //usleep(250*1000);
    }
}

