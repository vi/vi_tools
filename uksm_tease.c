#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc<3) {
        fprintf(stderr, "Usage uksm_tease page_size number_of_pages < /dev/urandom\n");
        return 1;
    }
    
    size_t page_size = atoi(argv[1]);
    long long int number_of_pages = atoi(argv[2]);
    
    char buf[page_size];
    fread(&buf, 1, page_size, stdin);
    
    char* a = calloc(number_of_pages, page_size);
    if (!a) {
        perror("calloc");
        return 1;
    }
    
    int i;
    for (i=0; i<number_of_pages; ++i) {
        memcpy(a+page_size*i, buf, page_size);
    }
    
    printf("finished\n");
    for (;;) {
        sleep(3600);
    }
}
