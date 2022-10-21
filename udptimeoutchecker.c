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

static void serve1(int socket_fd) {
    char buf[128];
    memset(buf, 0, sizeof(buf));
    
    struct sockaddr_storage sa;
    socklen_t sa_len = sizeof(sa);
    
    ssize_t ret = recvfrom(socket_fd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&sa, &sa_len);
    
    if (ret == -1) {
        return;
    }
    
    unsigned long int secs1;
    unsigned long int nanos1;
    unsigned long int secs2;
    unsigned long int nanos2;
    
    if(sscanf(buf, "%lu%lu%lu%lu", &secs1, &nanos1, &secs2, &nanos2) != 4) {
        return;
    }
    
    if (secs1+secs2 > 600 || nanos1 >= 1000*1000*1000 || nanos2 >= 1000*1000*1000) {
        return;
    }
    
    if (fork() != 0) {
        // SIGCHLD is ignored, so there should be no zombies
        return;
    }
    // child process:
    
    struct timespec ts1, ts2;
    ts1.tv_sec = secs1;
    ts1.tv_nsec = nanos1;
    ts2.tv_sec = secs2;
    ts2.tv_nsec = nanos2;
    
    if (-1 == clock_nanosleep(CLOCK_MONOTONIC, 0, &ts1, NULL)) _exit(1);
    sendto(socket_fd, "B\n", 2, 0,  (struct sockaddr*)&sa, sa_len);
    if (-1 == clock_nanosleep(CLOCK_MONOTONIC, 0, &ts2, NULL)) _exit(2);
    sendto(socket_fd, "A\n", 2, 0,  (struct sockaddr*)&sa, sa_len);
    
    _exit(0);
}


int try(int s, int seconds) {
    fprintf(stderr, "Trying with timeout %d seconds...", seconds);
    
    struct timeval tv;
    tv.tv_sec = seconds + 2;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    char buf[128];
    int l;
    l = snprintf(buf, sizeof(buf), "0 0 %d 0\n", seconds);
    
    l = send(s, buf, l, 0);
    if (l == -1) {
        perror("send");
        return -1;
    }
    
    l = recv(s, buf, sizeof(buf), 0);
    if(l<1) { 
        fprintf(stderr, "Fail B...\n");
        return -1;
    }
    if(buf[0] != 'B') {
        fprintf(stderr, "Strange B\n");
        return -1;
    }
    
    
    l = recv(s, buf, sizeof(buf), 0);
    if(l<1) {
        fprintf(stderr, "FAIL\n");
        return -1;
    }
    if(buf[0] != 'A') {
        fprintf(stderr, "Strange A\n");
        return -1;
    }
    
    fprintf(stderr, "OK\n");
    return 0;
}

int one_experiment(int s) {
    int max_to = 1;
    int num_fails = 0;
    
    // Phase 1 - double the timeout until it fails
    for(;;) {
        if (try(s, max_to) == 0) {
            num_fails = 0;
            
            
            if (max_to >= 512) {
                return max_to;
            }
            
            max_to *= 2;
        } else {
            num_fails += 1;
            if (num_fails == 3) {
                break;
            }
        }
    }
    
    if (max_to == 1) return 0;
    
    int min_to = max_to / 2;
    
    for(;;) {
        if (max_to - min_to <= 2) {
            fprintf(stderr, "Intermediate result: %d\n", min_to);
            return min_to;
        }
        int candidate = (min_to + max_to) / 2;
        
        if (try(s, candidate) == 0) {
            min_to = candidate;
        } else {
            max_to = candidate;
        }
    }
}

static int numcmp(const void *p1, const void *p2) {
    return (*(const int*)p1) - (*(const int*)p2);
}


int probe(int s) {
    int exps[3];
    int i;
    for(i=0; i<sizeof(exps)/sizeof(exps[0]); ++i) {
        exps[i] = one_experiment(s);
    }
    qsort(exps, sizeof(exps)/sizeof(exps[0]), sizeof(exps[0]), &numcmp);
    
    printf("%d\n", exps[1]);
    
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 4 && argc != 6) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "    udptimeoutchecker serve listen_addr listen_port \n");
        fprintf(stderr, "    udptimeoutchecker probe bind_addr bind_port connect_addr connect_port\n");
        fprintf(stderr, "Probes time for which UDP connection though a NAT stays open.\n");
        fprintf(stderr, "Example:\n");
        fprintf(stderr, "    udptimeoutchecker serve 0.0.0.0 1909\n");
        fprintf(stderr, "    udptimeoutchecker probe 0.0.0.0 0 vi-server.org 1909\n");
        fprintf(stderr, "\n");
        return 1;
    }
    
    const char* cmd = argv[1];
    const char* bind_addr_ = argv[2];
    const char* bind_port_ = argv[3];
    const char* send_addr_ = argv[4];
    const char* send_port_ = argv[5];
    
    if (!!strcmp(cmd, "serve") && !!strcmp(cmd, "probe")) {
        fprintf(stderr, "Command should be 'serve' or 'probe'\n");
        return 1;
    }
    if (!strcmp(cmd,"probe") && argc != 6) {
        fprintf(stderr, "Not enough arguments for probe command\n");
        return 1;
    }
    
    {
        struct sigaction siga;
        memset(&siga, 0, sizeof(siga));
        siga.sa_handler = SIG_IGN;
        sigaction(SIGCHLD, &siga, NULL);
    }
    
    struct addrinfo hints;
    struct addrinfo *recv_addr;
    struct addrinfo *send_addr;
    
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
    
    if (!strcmp(cmd,"probe")) {
    
        hints.ai_flags &= !AI_PASSIVE;
        gai_error=getaddrinfo(send_addr_, send_port_, &hints, &send_addr);
        if (gai_error) { fprintf(stderr, "getaddrinfo 2: %s\n",gai_strerror(gai_error));   return 13;  }
        if (!send_addr) { fprintf(stderr, "getaddrinfo 2 returned no addresses\n");  return 15;  }
        if (send_addr->ai_next) {
            fprintf(stderr, "Warning: using only one of addresses retuned by getaddrinfo 2\n");
        }
    
    }
    
    int s;
    s = socket(recv_addr->ai_family, recv_addr->ai_socktype, recv_addr->ai_protocol);
    if (s == -1) { perror("socket"); return 7; }
    {
        int one = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    }
    if (bind(s, recv_addr->ai_addr, recv_addr->ai_addrlen)) {  perror("bind");  return 5;  }
    
    
    if (!strcmp(cmd,"serve")) {
        for(;;) {
            serve1(s);
            usleep(250*1000);
        }
    }
    
    if (!strcmp(cmd,"probe")) {
        if (-1 == connect(s, send_addr->ai_addr, send_addr->ai_addrlen)) {
            perror("connect");
            return 6;
        }
        return probe(s);
    }
}

