#include <sys/eventfd.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>

// Implemented by Vitaly "_Vi" Shukela in 2015, License=MIT.

int main(int argc, char* argv[])
{
    if (argc != 3 || !strcmp(argv[1],"--help")) {
        fprintf(stderr, "Usage: cgroup_memory_pressure_monitor {low|medium|critical} /sys/fs/cgroup/memory/your_cgroup\n");
        return 1;
    }

    int efd = eventfd(0,0);
    if (efd == -1) { perror("eventfd"); return 2; }

    int mp;
    {
        char buf[4096];
        snprintf(buf, sizeof(buf), "%s/memory.pressure_level", argv[2]);
        mp = open(buf, O_RDONLY);
        if (mp == -1) { perror("open memory.pressure_level"); return 3; }
    }

    int cgc;
    {
        char buf[4096];
        snprintf(buf, sizeof(buf), "%s/cgroup.event_control", argv[2]);
        cgc = open(buf, O_WRONLY);
        if (cgc == -1) { perror("open cgroup.event_control"); return 4; }
    }

    {
        char buf[128];
        int l;
        snprintf(buf, sizeof(buf), "%d %d %s%n", efd, mp, argv[1], &l);
        int ret = write(cgc, buf, l);
        if (ret == -1) { perror("write"); return 5; }
    }

    close(cgc);
    close(mp);

    for(;;) {
        uint64_t x;
        int ret = read(efd, &x, sizeof(x));

        if (ret == 0) break;
        if (ret == -1 && (errno == EINTR || errno == EAGAIN)) continue;
        if (ret != sizeof(x)) { perror("read"); return 6; }

        fprintf(stdout, "%"PRId64"\n", x);
        fflush(stdout);
    }

    return 0;
}
