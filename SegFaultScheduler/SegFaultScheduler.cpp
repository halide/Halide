#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

int *data;

const size_t chunk_size = 1600*1024;
const size_t size = 100*1024*1024;

const int K = 4;
int *chunks[K];
int lru_chunk = 0;
int *chunk_mapping[K];
pthread_mutex_t chunk_mutex = PTHREAD_MUTEX_INITIALIZER;

// produce a chunk of data
void produce_data(int *chunk) {
    for (int i = 0; i < chunk_size; i++) {
	chunk[i] = i;
    }
}

// consume a chunk of data
int consume_data(int *chunk) {
    int acc = 0;
    for (int i = 0; i < chunk_size; i++) {
	acc += chunk[i];
    }
    return acc;
}

void *malloc_aligned(size_t s) {
    char *ptr = (char *)malloc(s + 0xfff);
    char *newPtr = ptr - ((size_t)ptr & 0xfff) + 4096;
    ((void **)newPtr)[-1] = ptr;
    return (void *)newPtr;
}

void free_aligned(void *ptr) {
    free(((void **)ptr)[-1]);
}

void segfault_sigaction(int signal, siginfo_t *si, void *arg) {
    size_t idx = (int *)si->si_addr - data;

    if (idx > size) {
	printf("Legit segfault\n");
	// this is a legit segfault
	exit(1);
    }

    // drop it to the nearest chunk boundary
    idx -= (idx % chunk_size);

    //printf("Index = %ld\n", idx);

    // Grab a mutex
    pthread_mutex_lock(&chunk_mutex);

    // We're going to use the lru_chunk to store this data.
    // First, reprotect whatever lru_chunk used to point to.
    if (chunk_mapping[lru_chunk]) {
        mprotect(chunk_mapping, chunk_size*sizeof(int), PROT_NONE);
        chunk_mapping[lru_chunk] = data + idx;
    }

    // Then, remap data + idx to point to the lru_chunk and allow read/write
    remap_file_pages(data + idx, chunk_size*sizeof(int), 0,
                     (lru_chunk*chunk_size*sizeof(int)) / 4096, 0);
    mprotect(data + idx, chunk_size*sizeof(int), PROT_WRITE);
   
    // update lru_chunk so that a different chunk is used next time
    lru_chunk++;
    if (lru_chunk == K) lru_chunk = 0;

    // Drop the mutex
    pthread_mutex_unlock(&chunk_mutex);

    // produce the page
    produce_data(data + idx);        
}

struct thread_info {
    int thread_id, threads, ret;
};

void *do_eager(void *x) {    
    thread_info *info = (thread_info *)x;
    printf("Launched thread %d\n", info->thread_id);
    for (int i = info->thread_id*chunk_size; i < size; i += chunk_size*info->threads) {
        produce_data(data + i);
    }
    
    info->ret = 0;
    for (int i = info->thread_id*chunk_size; i < size; i += chunk_size*info->threads) {
        info->ret += consume_data(data + i);
    }	

    return NULL;
}

void *do_lazy(void *x) {
    thread_info *info = (thread_info *)x;
    printf("Launched thread %d\n", info->thread_id);
    info->ret = 0;
    for (int i = info->thread_id*chunk_size; i < size; i += chunk_size*info->threads) {
        info->ret += consume_data(data + i);
    }	

    return NULL;
}

void *do_static(void *x) {
    thread_info *info = (thread_info *)x;
    printf("Launched thread %d\n", info->thread_id);
    info->ret = 0;
    for (int i = info->thread_id*chunk_size; i < size; i += chunk_size*info->threads) {
        produce_data(data);
        info->ret += consume_data(data);
    }
    
    return NULL;
}

int main(int argc, char **argv) {

    int threads = argc > 1 ? atoi(argv[1]) : 1;
    pthread_t *thread = new pthread_t[threads];
    thread_info *info = new thread_info[threads];
    for (int i = 0; i < threads; i++) {
        info[i].thread_id = i;
        info[i].threads = threads;
        info[i].ret = 0;
    }
    printf("Launching %d threads\n", threads);

    // Install a segfault handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segfault_sigaction;
    sa.sa_flags   = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);

    struct timeval t1, t2;

    {
	data = (int *)malloc_aligned(sizeof(int)*size);

	gettimeofday(&t1, NULL);

        for (int i = 0; i < threads; i++)
            pthread_create(thread+i, NULL, do_eager, info+i);

        int sum = 0;
        for (int i = 0; i < threads; i++) {
            pthread_join(thread[i], NULL);
            sum += info[i].ret;
        }


	gettimeofday(&t2, NULL);

	printf("Eagerly scheduled sum    = %d (%ld us, %ld bytes)\n", sum, 
	       (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec - t1.tv_usec),
               sizeof(int)*size);


	free_aligned(data);
    }



    {
	
	// Allocate K chunks of actual memory
	int fd = open("/tmp/foo", O_RDWR | O_CREAT);
	ftruncate(fd, size*sizeof(int));
	for (int i = 0; i < K; i++) {
	    chunks[i] = (int *)mmap(NULL, sizeof(int)*chunk_size,
				    PROT_WRITE, MAP_SHARED, 
				    fd, i * chunk_size * sizeof(int));
            chunk_mapping[i] = NULL;
	}

	// Initialize data to map the entire file. We're not going
	// to use it though, instead we'll remap pieces of it back to
	// the chunks above as needed.
        data = (int *)mmap(NULL, sizeof(int)*size, PROT_NONE, MAP_SHARED, fd, 0);

        unlink("/tmp/foo");

	gettimeofday(&t1, NULL);
	
        for (int i = 0; i < threads; i++)
            pthread_create(thread+i, NULL, do_lazy, info+i);

        int sum = 0;
        for (int i = 0; i < threads; i++) {
            pthread_join(thread[i], NULL);
            sum += info[i].ret;
        }
        
	gettimeofday(&t2, NULL);

	close(fd);	
	
	printf("Lazily scheduled sum     = %d (%ld us, %ld bytes)\n", sum, 
	       (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec - t1.tv_usec),
               sizeof(int)*chunk_size*K);
	

	munmap(data, size*sizeof(int));
	for (int i = 0; i < K; i++) {
	    munmap(chunks[i], chunk_size);
	}


    }


    // try a static schedule into some scratch

    {
	data = (int *)malloc_aligned(sizeof(int)*chunk_size);

	gettimeofday(&t1, NULL);

        for (int i = 0; i < threads; i++)
            pthread_create(thread+i, NULL, do_static, info+i);

        int sum = 0;
        for (int i = 0; i < threads; i++) {
            pthread_join(thread[i], NULL);
            sum += info[i].ret;
        }

	gettimeofday(&t2, NULL);

	printf("Statically scheduled sum = %d (%ld us, %ld bytes)\n", sum, 
	       (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec - t1.tv_usec),
               sizeof(int)*chunk_size);       


	free_aligned(data);
    }



    return 0;
}
