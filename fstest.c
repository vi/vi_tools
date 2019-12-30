/*
 * fstest.c: Test file system (like fusecompress) for failures
 *
 * Copyright (c) 2008 Vitaly "_Vi" Shukela. Some rights reserved.
 * GPLv2
 *
 */

static const char rcsid[] = "$Id:$";    
#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE // Include O_DIRECT
#include <stdio.h> 
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>        
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <malloc.h>
#ifdef SIGNAL
    #include <signal.h>
    int interrupted = 0;
    void int_handler (int a) {
	++interrupted;
	if (interrupted == 1) {
	    fprintf(stderr, "Interrupt signal, going to \"cleaning\" phase\n");
	} 
	if (interrupted>1) {
	    fprintf(stderr, "Interrupt signal, exiting\n");
	    exit(2);
	}	
    }
    struct sigaction inta = { int_handler, 0, 0, 0, 0 };
#endif

#define MAX_FSYNC_PART 1000000

/**
 * User-definable program parameters
 */
struct params {
    char* filename;
    off64_t size;
    int num_iterations;
    size_t max_blocksize;
    char* random_filename;
    int max_chainlen;
    int sync_part;
};

/**
 * Checker state. Stores XOR checksums for the whole file and various parts of it.
 * Elements k from 0 to 7 -- xor of bytes with position with k'th bit set
 * Elements 8 -- global xor.
 */
struct state{
   unsigned char xors[9];
};

void update_xor(struct state *s, unsigned char v, off64_t pos){
    //fprintf(stderr,"xor %d at %d\n",v,pos);
    int i;
    for(i=0; i<8; ++i){
	unsigned char vv = (pos & (1 << i)) ? v : 0;
	s->xors[i] ^= vv;
    }
    s->xors[8] ^= v;
}

typedef ssize_t (*PFunc)(int, void*, size_t, off64_t);

/**
 * Call pread or pwrite for the full buffer, handling signal interrupts
 * @returns nonzero if failure
 */
int safe_fileop( PFunc func, int file, off64_t pos, size_t len, unsigned char *buf ){
    ssize_t ret;
    size_t rest=len;
    off64_t ppos=pos;
    while(rest){
	//fprintf(stderr, "file=%d, rest=%d, ppos=%d -> ", file, rest, ppos);
	ret = func(file, buf, rest, ppos);
	//fprintf(stderr, "%d\n",ret);
	if(ret==0){
	    while(rest){
		*buf++=0;
		--rest;
		++ppos;
	    }
	    return 0;
	}
	if(ret==-1){
	    if(errno==EAGAIN || errno==EINTR){ continue; }
	    perror("pread/pwrite"); 
	    return 1;
	}
	rest-=ret;
	ppos+=ret;
	buf+=ret;
    }
    return 0;
}

/**
 * Read and "neutralize" specified block, than generate, "register" and write new block.
 * @param file File handle to read/write.
 * @param random /dev/urandom descriptor
 * @param pos Position of block in bytes
 * @param len Length of block in bytes
 * @param buf Butter of length greater than len.
 * @param mode 0 -- normal, 1 -- just read, 2 -- nosync
 * @return nonzero if failure
 */
int insert_block(struct state *st, int file, off64_t pos, size_t len, unsigned char *buf, const unsigned char *newdata, int mode){    
    int i;

    if(safe_fileop(pread, file, pos, len, buf)){return 1;}

    // Remove this block from hash
    for(i=0; i<len; ++i){
	update_xor(st, buf[i], pos+i);
    }

    if(mode!=1){
	if(safe_fileop((PFunc)pwrite, file, pos, len, (unsigned char *)newdata)){return 1;}

	// Remove this block from hash
	for(i=0; i<len; ++i){
	    update_xor(st, newdata[i], pos+i);
	}
    }

    if(mode==0){
	fsync(file);
    }

    return 0;
}

/**
 * function to read /dev/urandom with pread signature
 */
ssize_t fake_pread(int fd, void *buf, size_t count, off64_t offset){
    return read(fd, buf, count);
}

#define RANDOMIZE(variable)\
if(safe_fileop(fake_pread, random, 0, sizeof(variable),            (void*)&(variable))){return 1;}

int generate_block(int random, size_t len, unsigned char *buf){
    unsigned char mode;
    int i;
    RANDOMIZE(mode);
    mode%=4;
    switch(mode){
	case 0: // Random fill
	case 1: 
	    if(safe_fileop(fake_pread, random, 0, len, buf)){return 1;}
	    break;
	case 2: // Byte fill
	    {
		unsigned char c;
		RANDOMIZE(c);
		for(i=0; i<len; ++i){
		    buf[i]=c;
		}
	    }
	    break;
	case 3: // 32bit pattern fill
	    {
		unsigned long c;
		RANDOMIZE(c);
		for(i=0; i<len/4; ++i){
		    ((unsigned long*)buf)[i]=c;
		}
	    }
	    break;
    }
    return 0;
}

/**
 * Insert series of equal generated buffers at accending offsets at random place in file.
 * @param st Hash state
 * @param p Program parameters
 * @param file File handle
 * @param random /dev/urandom handle
 * @param buf1 Just a preallocated buffer of length at least p->max_blocksize
 * @param buf2 Just a preallocated buffer of length at least p->max_blocksize
 * @return nonzero if failure
 */
int insert_chain(struct state *st, struct params *p, int file, int random, unsigned char* buf1, unsigned char* buf2){
    off64_t offset; 
    unsigned int repetitions_count;
    size_t length;

    RANDOMIZE(offset);
    RANDOMIZE(repetitions_count);
    RANDOMIZE(length);

    if(offset<0)offset=-offset;
    offset            %= p->size;
    length            %= p->max_blocksize;
    repetitions_count %= p->max_chainlen;
    ++repetitions_count;

    if(generate_block(random, length, buf2)){return 1;}

    //fprintf(stderr,"Inserting chain of %d blocks of length %d to offset %d\n",repetitions_count, length, offset);

    for(;repetitions_count;--repetitions_count){
	if(offset+length > p->size){
	    offset=0;
	    repetitions_count=1;
	}

	int sync;
	if (p->sync_part == MAX_FSYNC_PART) {
	    sync = 1;
	}
	else if (p->sync_part == 0) {
	    sync = 0;
	} else {
	    unsigned int syncv;
	    unsigned long long int syncp = p->sync_part;
	    RANDOMIZE(syncv);
	    syncp<<=sizeof(unsigned int)*8;
	    syncp/=MAX_FSYNC_PART;
	    sync = syncv < syncp;
	}

        if(insert_block(st, file, offset, length, buf1, buf2, sync?0:2 /* 2 - nosync */ )){return 1;} 

	offset+=length;
    }
    return 0;
}


/**
 * Returns 1 every statblock invocations
 */
int statoutput(int statblock, int *state, int increment){
    *state+=increment;
    if(*state>statblock){
	*state=0;
	return 1;
    }
    return 0;
}

/**
 * Zero out the whole file to zero our hash state.
 * If it will not be zero in the end, something has failed.
 * @returns nonzero if failure (hash!=0 is not a failure)
 */
int cleanup(struct state *s, struct params *p, int file, unsigned char *buf1, unsigned char *buf2, int mode){
    int i;
    for(i=0; i < p->max_blocksize; ++i){
	buf2[i]=0;
    }

    off_t size = p->size;

    if(mode==1) {
	struct stat stbuf;
	fstat(file, &stbuf);
	size = stbuf.st_size+p -> max_blocksize;
	if(p->size < size) {
	    size = p->size;
	}
    }

    long long cleanups = size / p->max_blocksize;
    int statstate=0;
    i=0;

    off64_t off=0;
    while(off <= size){
	if(insert_block(s, file, off, p->max_blocksize, buf1, buf2, 1/*no write*/)){return 1;}
	off += p->max_blocksize;

	++i;
	if(statoutput(4*1024*1024, &statstate, p->max_blocksize)){
	    printf(mode?"%d of %d prereads\n":"%d of %d cleanups\n", i, cleanups);	    
	}
    }
    return 0;

}

/**
 * Pase command line arguments
 * @return nonzero if failure
 */
int parse_args(int argc, char* argv[], struct params *p){
    if(argc<2){
	printf("fstest filename [size] [iterations] [max_blocksize] [max_chainlen] [random_source] [fsync_part]\n"
		"\tTest filesystem (like fusecompress) for failures, Version 0.1.3\n"
		"\tMax_blobksize in bytes, max_chainlen and iterations is integer, filename and random_source is a string\n"
		"\tfsync_part is integer from 0 to 1000000 - probability of fsync after each write\n"
		"\tOverride random_source to get reproducible behaviour\n"
		"\tDefaults are: 100M 65536 4096 64 /dev/urandom 10000\n"
		"\tIf you are tired of waiting for iterations, press ^C to go to clean phase\n"
		"\tExample: fstest qqq $((1024*1024)) 8192 1024 32\n");
	return 1;
    }
    if(*++argv){ p->filename        =       *argv ;   }else{return 0;}
    if(*++argv){ p->size            = atoll(*argv);   }else{return 0;}
    if(*++argv){ p->num_iterations  = atoi (*argv);   }else{return 0;}
    if(*++argv){ p->max_blocksize   = atoi (*argv);   }else{return 0;}
    if(*++argv){ p->max_chainlen    = atoi( *argv);   }else{return 0;}
    if(*++argv){ p->random_filename =       *argv ;   }else{return 0;}
    if(*++argv){ p->sync_part    = atoi (*argv);   }else{return 0;}
    return 0;
}


/** 
 * Open file and random numbers source 
 * @returns nonzero if failure
 */
int prepare(int *file, int *random, struct params *p){

    //unlink(p->filename);

    *file=open(p->filename, O_RDWR|O_CREAT, 0777);
    if(*file==-1){perror("open"); return 1;}

    *random=open(p->random_filename ,O_RDONLY);
    if(*random==-1){perror("open (of random)"); return 1;}

    return 0;
}

int main(int argc, char* argv[]){

                  /* filename      size             num_iterations   max_blocksize  random_filename  max_chainlen sync_part*/
    struct params p={"fstest.dat", 1024*1024*100,   64*1024,         4096,          "/dev/urandom",  64,          MAX_FSYNC_PART/100};
    int file, random;
    struct state  s={{0,0,0,0,0,0,0,0,0}};
    int i;
    int statstate=0;

    if(parse_args(argc, argv, &p)){return 1;}

    if(prepare(&file, &random, &p)){return 1;}

    unsigned char *buf1, *buf2;
    buf1=(unsigned char*)malloc(p.max_blocksize);
    buf2=(unsigned char*)malloc(p.max_blocksize);
    //unsigned char buf1[p.max_blocksize], buf2[p.max_blocksize];

    #ifdef SIGNAL
	sigaction(SIGINT, &inta, NULL); 
    #endif
    
    if(cleanup(&s, &p, file, buf1, buf2, 1));
	
    for(i=0; i<p.num_iterations; ++i){
	if(interrupted) { printf("Stopping iterations due to signal\n"); break; }

	if(insert_chain(&s, &p, file, random, buf1, buf2)){return 1;}
	
	if(statoutput(256, &statstate, p.max_chainlen+p.max_blocksize/10000 )){
	    printf("iteration %d of %d\n",i, p.num_iterations);
	}
    }

    if(cleanup(&s, &p, file, buf1, buf2, 0)){return 1;}

    //unlink(p.filename);

    for(i=0; i<9; ++i){
	if(s.xors[i]){
	    printf("fail\n");
	    return 2;
	}
    }

    printf("OK\n");

    free(buf1);
    free(buf2);

    return 0;
}
