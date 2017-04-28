#include <stdio.h>
#include <malloc.h>
int main() {
    
    FILE* oom_score_adj = fopen("/proc/self/oom_score_adj","w");
    if(oom_score_adj) {
        fprintf(oom_score_adj,"1000\n");
        fclose(oom_score_adj);
    } else {
        FILE* oom_adj = fopen("/proc/self/oom_adj","w");
        if(oom_adj) {
            fprintf(oom_adj,"15\n");
            fclose(oom_adj);
        } else {
            fprintf(stderr, "Failed to raise OOM score\n");
            fprintf(stderr, "Are you fast enough to ^C before the havoc begins?\n");
        }
    }
    
    for(;;) { 
        char* c=malloc(655360);
        int i;
        for(i=0;i<655360;++i) {
            c[i]=0; 
        }
    }
}
