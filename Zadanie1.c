#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>


#define ALPHABET_SIZE 256

// result  slot  for each child
typedef struct {
    long counts[ALPHABET_SIZE];
} WorkerResult;

// What  parent knows about the mapped file
typedef struct {
    char  *data;
    size_t size;
} MappedFile;


//mapping file mmap
static int map_file(const char *path, MappedFile *out)
{
    int fd = open(path, O_RDONLY);
    if (fd == -1) { perror("open"); return -1; }

    struct stat sb;
    if (fstat(fd, &sb) == -1) { perror("fstat"); close(fd); return -1; }

    out->size = (size_t)sb.st_size;
    out->data = mmap(NULL, out->size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (out->data == MAP_FAILED) { perror("mmap (file)"); return -1; }
    return 0;
}


// unmaping mapped file 
static void unmap_file(MappedFile *f)
{
    if (f->data && f->data != MAP_FAILED)
        munmap(f->data, f->size);
}


// prinnting to std 
static void print_file_contents(const MappedFile *f)
{
    printf("File contents: \n");
    write(STDOUT_FILENO, f->data, f->size);
    printf("\n");
}


// creating shared  memory 
static WorkerResult *create_shared_memory(int n)
{
    size_t size = (size_t)n * sizeof(WorkerResult);
    WorkerResult *mem = mmap(NULL, size,PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS,-1, 0);
    if (mem == MAP_FAILED) { perror("mmap (shared)"); return NULL; }
    memset(mem, 0, size);
    return mem;
}

// destroying mem 
static void destroy_shared_memory(WorkerResult *mem, int n)
{
    if (mem && mem != MAP_FAILED)
        munmap(mem, (size_t)n * sizeof(WorkerResult));
}

//counting childs characters 
static void count_chars(const char *buf, size_t len, long out[ALPHABET_SIZE])
{
    for (size_t i = 0; i < len; i++)
        out[(unsigned char)buf[i]]++;
}


// 3 % crash 
static void maybe_crash(int worker_id)
{
    srand((unsigned)(time(NULL) ^ (unsigned long)getpid()));
    if ((rand() % 100) < 3) {
        fprintf(stderr, "[worker %d] crashd \n", worker_id);
        abort();
    }
}

//child work 
static void child_work(int worker_id, const MappedFile *f, WorkerResult *shared,int workers)
{
    size_t slice = f->size / (size_t)workers;
    size_t start = (size_t)worker_id * slice;
    size_t end   = (worker_id == workers - 1) ? f->size : start + slice;

    // each child count its part 
    long local[ALPHABET_SIZE];
    memset(local, 0, sizeof(local));
    count_chars(f->data + start, end - start, local);

    // chance  of  crashing 
    maybe_crash(worker_id);

    // write into shared  mem 
    memcpy(shared[worker_id].counts, local, sizeof(local));

    // clean 
    unmap_file((MappedFile *)f);
    destroy_shared_memory(shared, workers);
    exit(EXIT_SUCCESS);
}

// creating childeren 
static int spawn_workers(int workers,const MappedFile *f,WorkerResult *shared, pid_t pids[])
{
    for (int i = 0; i < workers; i++) {
        pids[i] = fork();
        if (pids[i] == -1) { perror("fork"); return -1; }

        if (pids[i] == 0) {
            child_work(i, f, shared, workers); /* never returns */
        }
    }
    return 0;
}

//waiting for workers 
static int wait_for_workers(int workers, const pid_t pids[])
{
    int all_ok = 1;

    for (int i = 0; i < workers; i++) {
        int status;
        //wait for  processes 
        pid_t finished = waitpid(pids[i], &status, 0);

        if (finished == -1) { perror("waitpid"); all_ok = 0; continue; }

        if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
            fprintf(stderr, "[parent] worker %d (pid %d) terminated abnormally\n", i, pids[i]);
            all_ok = 0;
        }
    }
    return all_ok;
}

//writing into one  file 
static void aggregate_results(const WorkerResult *shared,int workers,long totals[ALPHABET_SIZE])
{
    memset(totals, 0, ALPHABET_SIZE * sizeof(long));
    for (int i = 0; i < workers; i++)
        for (int c = 0; c < ALPHABET_SIZE; c++)
            totals[c] += shared[i].counts[c];
}


// Parent printing the  summary 
static void print_summary(const long totals[ALPHABET_SIZE])
{
    printf("\n Character number: \n");
    for (int c = 0; c < ALPHABET_SIZE; c++) {
        if (totals[c] == 0) continue;
        if (c >= 32 && c < 127)
            printf("  '%c'  (0x%02X) : %ld\n", c, c, totals[c]);
        else
            printf("  0x%02X        : %ld\n", c, totals[c]);
    }
}

// parsing arguments 
static int parse_args(int argc, char **argv,const char **filename, int *workers)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file> [num_processes]\n", argv[0]);
        return -1;
    }
    *filename  = argv[1];
    if(argc >= 3)
        *workers = atoi(argv[2]);

    if (*workers < 1) {
        fprintf(stderr, "Number of processes must be >= 1\n");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    const char *filename;
    int workers = 1; // domyślna wartość

    if (parse_args(argc, argv, &filename, &workers) != 0)
        return EXIT_FAILURE;

    MappedFile f;
    if (map_file(filename, &f) != 0)
        return EXIT_FAILURE;

    print_file_contents(&f);

    WorkerResult *shared = create_shared_memory(workers);
    if (!shared) { 
        unmap_file(&f); 
        return EXIT_FAILURE;
    }

    
    pid_t *pids = (pid_t *)malloc((size_t)workers * sizeof(pid_t));
    if (pids == NULL) {
        perror("malloc (pids)");
        unmap_file(&f);
        destroy_shared_memory(shared, workers);
        return EXIT_FAILURE;
    }

    srand((unsigned)time(NULL));

    if (spawn_workers(workers, &f, shared, pids) != 0) {
        unmap_file(&f);
        destroy_shared_memory(shared, workers);
        free(pids); // zawlaniamy jak blad
        return EXIT_FAILURE;
    }

    int all_ok = wait_for_workers(workers, pids);

    if (!all_ok) {
        printf("\n Failed: worker crashed \n");
    } else {
        long totals[ALPHABET_SIZE];
        aggregate_results(shared, workers, totals);
        print_summary(totals);
    }

    unmap_file(&f);
    destroy_shared_memory(shared, workers);
    free(pids); // zwolnienie pamiec 

    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}