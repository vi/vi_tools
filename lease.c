#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include <sys/signalfd.h>   
#include <signal.h>

char** cmdline;

void sigio(int signaln) {
    execvp(cmdline[0], cmdline);
    exit(127);
}

int main(int argc, char* argv[]) {
    if (argc < 3 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-?")) {
        fprintf(stderr, "Usage: lease {filename|@fdnum} {ro|rw} -- command line\n");
        fprintf(stderr, "   do F_SETLEASE and execute command line one on SIGIO \n");
        fprintf(stderr, "Example: lease qwe rw -- sleep 2 # delays the next open of the file qwe by 2 seconds\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "Usage 2: lease @fdnum {rw|ro|no|get}\n");
        fprintf(stderr, "  just set, remove or print the lease without waiting for SIGIO\n");
        return 1;
    }
    
    const char* filename = argv[1];
    const char* mode_s = argv[2];
    
    int has_command_line = 0;
    if (argc > 4) has_command_line = 1;
    
#define GETMODE ((F_RDLCK|F_WRLCK|F_UNLCK)+1)

    int mode;
    
    if (!strcmp(mode_s, "ro")) {
        mode = F_RDLCK;
    } else 
    if (!strcmp(mode_s, "rw")){
        mode = F_WRLCK;
    } else 
    if (!strcmp(mode_s, "no")){
        mode = F_UNLCK;
        if (has_command_line) {
            fprintf(stderr, "lease: Command line is meaningless in lease removal mode\n");
            return 1;
        }
    } else 
    if (!strcmp(mode_s, "get")){
        mode = GETMODE;
        if (has_command_line) {
            fprintf(stderr, "lease: Command line is meaningless when just getting lease info\n");
            return 1;
        }
    } else {
        fprintf(stderr, "lease: Invalid mode %s\n", mode_s);
        return 1;
    }
    
    if (has_command_line) {
        const char* dashes = argv[3];
        cmdline = &argv[4];
        
        if (!!strcmp(dashes, "--")) {
            fprintf(stderr, "lease: Third command line argument must be two hypens: --\n");
            return 1;
        }
        
        struct sigaction new_action;
    
        new_action.sa_handler = &sigio;
        sigemptyset (&new_action.sa_mask);
        //sigaddset(&new_action.sa_mask, SIGIO);
        new_action.sa_flags = 0;
        
        sigaction (SIGIO, &new_action, NULL);
    }
    
    int fd;
    if (filename[0] == '@') {
        if(sscanf(filename, "@%d", &fd) != 1) {
            fprintf(stderr, "Invalid file descriptor number %s\n", filename);
            return 1;
        }
    } else {
        fd = open(filename, O_RDONLY, 0777);
    }
    
    if (fd==-1) {
        perror("open");
        return 2;
    }
    
    if (mode == GETMODE) {
        int ret = fcntl(fd, F_GETLEASE, 0);
        if (ret == -1) {
            perror("fcntl(GETLEASE)");
            return 3;
        }
        
        switch(ret) {
            case F_RDLCK: printf("F_RDLCK\n"); break;
            case F_WRLCK: printf("F_WRLCK\n"); break;
            case F_UNLCK: printf("F_UNLCK\n"); break;
            default: printf("%d\n", ret); break;
        }
        
    } else {
        int ret = fcntl(fd, F_SETLEASE, mode);
        if (ret == -1) {
            perror("fcntl(SETLEASE)");
            return 3;
        }
    }
    
    if (has_command_line) {
        pause();
        return 127;
    } else {
        return 0;
    }
}
