#include <dlfcn.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char* argv[], char* envp[]) {
    if(argc<3) {
        write(2, "Usage: runso ./libsomelibrary.so main <args>\n", 63-18);
        return 1;
    }
    void* d = dlopen(argv[1], RTLD_LAZY);
    if(!d) {
        char* e = dlerror();
        write(2, e, strlen(e));
        write(2, "\n", 1);
        return 2;
    }
    void* m = dlsym(d, argv[2]);
    if(!m) {
        char* e = dlerror();
        write(2, e, strlen(e));
        write(2, "\n", 1);
        return 3;
    }
    return ((int(*)(int,char**,char**))m)(argc-2, argv+2, envp);
}

