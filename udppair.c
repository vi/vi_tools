#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>

char buf[4096];

int udp_socket(int port) {
    int s2;
    struct sockaddr_in si_me;

    if ((s2=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
        perror("socket");
        return -1;
    }
 
    // si_me stores our local endpoint. Remember that this program
    // has to be run in a network with UDP endpoint previously known
    // and directly accessible by all clients. In simpler terms, the
    // server cannot be behind a NAT.
    memset((char *) &si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(port);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s2, (struct sockaddr*)(&si_me), sizeof(si_me))==-1) {
        perror("bind");
        return -1;
    }
    return s2;
}

int main(int argc, char* argv[])
{
    if (argc<3) {
        fprintf(stderr, "Usage: udppair port1 port2\n");
        return 1;
    }
    int port1 = atoi(argv[1]);
    int port2 = atoi(argv[2]);

    int s1 = udp_socket(port1);
    int s2 = udp_socket(port2);

    fcntl(s1, F_SETFL, O_NONBLOCK);
    fcntl(s2, F_SETFL, O_NONBLOCK);

    fd_set rfds;

    struct sockaddr_in peer1;
    struct sockaddr_in peer2;
    memset((char *) &peer1, 0, sizeof(peer1));
    memset((char *) &peer2, 0, sizeof(peer2));

    for(;;) {
select_repeat:
        FD_ZERO(&rfds);
        FD_SET(s1, &rfds);
        FD_SET(s2, &rfds);
        int maxfd = (s1>s2)?s1:s2;
        int ret = select(maxfd+1, &rfds, NULL, NULL, NULL);

        if(ret==-1) {
            if(errno==EINTR  || errno==EAGAIN) goto select_repeat;
            perror("select");
            return 2;
        }
        
        if(FD_ISSET(s1, &rfds)) {
            socklen_t slen = sizeof(peer1);
            ret = recvfrom(s1, &buf, sizeof(buf), 0, (struct sockaddr*)(&peer1), &slen);
            if(ret==-1) {
                perror("recvfrom");
                return 4;
            }
            if(ret) {
                ret = sendto(s2, &buf, ret, 0, (struct sockaddr*)(&peer2), slen);
                if (!ret) {
                    perror("sendto");
                } else {
                    write(1, ">", 1);
                }
            }
        }
        if(FD_ISSET(s2, &rfds)) {
            socklen_t slen = sizeof(peer2);
            ret = recvfrom(s2, &buf, sizeof(buf), 0, (struct sockaddr*)(&peer2), &slen);
            if(ret==-1) {
                perror("recvfrom");
                return 4;
            }
            if(ret) {
                ret = sendto(s1, &buf, ret, 0, (struct sockaddr*)(&peer1), slen);
                if (!ret) {
                    perror("sendto");
                } else {
                    write(1, "<", 1);
                }
            }

        }
    }    
}
