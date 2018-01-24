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

static void serve1(int socket_fd, struct sockaddr* to, socklen_t to_len) {
    char buf[128];
    memset(buf, 0, sizeof(buf));
    ssize_t ret = recv(socket_fd, buf, sizeof(buf)-1, 0);
    
    if (ret == -1) {
        if (errno == EAGAIN) {
            // Timeout, need to peck
            sendto(socket_fd, "", 0, 0, to, to_len);
        }
        return;
    }
    
    int fdnum=-1, val=-1, len;
    if (sscanf(buf, "%d%d", &fdnum, &val) != 2) return;
    
    if (fdnum != 1 && fdnum < 3 && fdnum > 1000) return;
    
    len = snprintf(buf, sizeof(buf), "%d\n", val);
    
    if (len <= 0 || len > sizeof(buf)) return;
    
    write(fdnum, buf, len);
}

int main(int argc, char* argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: udpfilepecker listen_addr listen_port peck_addr peck_port peck_period_sec\n");
        fprintf(stderr, "Listens UDP port, periodically pings peck_addr:peck_port, for for each incoming UDP packet of the format of two integer values 'fd_num value' it writes value to the specified FD.\n");
        fprintf(stderr, "Sample: udpfilepecker 0.0.0.0 5599 104.131.203.210 5598 30 3> /sys/class/backlight/intel_backlight/brightness\n");
        return 1;
    }
    
    const char* listen_addr = argv[1];
    const char* listen_port = argv[2];
    const char* peck_addr = argv[3];
    const char* peck_port = argv[4];
    const char* peck_period = argv[5];
    
    struct addrinfo hints;
    struct addrinfo *recv_addr;
    struct addrinfo *send_addr;
    
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = AI_PASSIVE;
    
    int gai_error;
    gai_error=getaddrinfo(listen_addr,listen_port, &hints, &recv_addr);
    if (gai_error) { fprintf(stderr, "getaddrinfo 1: %s\n",gai_strerror(gai_error)); return 4; }
    if (!recv_addr) { fprintf(stderr, "getaddrinfo returned no addresses\n");   return 6;  }
    if (recv_addr->ai_next) {
        fprintf(stderr, "Warning: using only one of addresses retuned by getaddrinfo\n");
    }
    
    hints.ai_flags &= !AI_PASSIVE;
    gai_error=getaddrinfo(peck_addr, peck_port, &hints, &send_addr);
    if (gai_error) { fprintf(stderr, "getaddrinfo 2: %s\n",gai_strerror(gai_error));   return 13;  }
    if (!send_addr) { fprintf(stderr, "getaddrinfo 2 returned no addresses\n");  return 15;  }
    if (send_addr->ai_next) {
        fprintf(stderr, "Warning: using only one of addresses retuned by getaddrinfo 2\n");
    }
    
    int s;
    s = socket(recv_addr->ai_family, recv_addr->ai_socktype, recv_addr->ai_protocol);
    if (s == -1) { perror("socket"); return 7; }
    {
        int one = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    }
    if (bind(s, recv_addr->ai_addr, recv_addr->ai_addrlen)) {  perror("bind");  return 5;  }
    
    struct timeval tv;
    tv.tv_sec = atoi(peck_period);
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    mlockall(MCL_CURRENT|MCL_FUTURE);
    nice(-19);
    
    for(;;) {
        serve1(s, send_addr->ai_addr, send_addr->ai_addrlen);
    }
    
    return 0;
}

