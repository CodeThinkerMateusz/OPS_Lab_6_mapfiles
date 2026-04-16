#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>

#define KEYBOARD_CAP 10
#define SHARED_MEM_NAME "/memory"
#define MIN_STUDENTS KEYBOARD_CAP
#define MAX_STUDENTS 20
#define MIN_KEYBOARDS 1
#define MAX_KEYBOARDS 5
#define MIN_KEYS 5
#define MAX_KEYS KEYBOARD_CAP

#define ERR(source)                                     \
    do                                                  \
    {                                                   \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        perror(source);                                 \
        kill(0, SIGKILL);                               \
        exit(EXIT_FAILURE);                             \
    } while (0)


typedef struct {
    pthread_barrier_t barrier;
    pthread_mutex_t keys_mutexes[MAX_KEYBOARDS * MAX_KEYS];
    double keys_state[MAX_KEYBOARDS * MAX_KEYS];
} SharedData;


void usage(char* program_name)
{
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "\t%s n m k\n", program_name);
    fprintf(stderr, "\t  n - number of students, %d <= n <= %d\n", MIN_STUDENTS, MAX_STUDENTS);
    fprintf(stderr, "\t  m - number of keyboards, %d <= m <= %d\n", MIN_KEYBOARDS, MAX_KEYBOARDS);
    fprintf(stderr, "\t  k - number of keys in a keyboard, %d <= k <= %d\n", MIN_KEYS, MAX_KEYS);
    exit(EXIT_FAILURE);
}

char* concat(const char *s1, const char *s2)
{
    char *result = malloc(strlen(s1) + strlen(s2) + 1); // +1 for the null-terminator
    if(result == NULL){
        perror("couldn't malloc");
    }
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

char* get_semname(int i){
    char num[12];
    snprintf(num,sizeof(num), "%d",i+1);
    return concat("/sop-sem-",num);
}

void ms_sleep(unsigned int milli)
{
    time_t sec = (int)(milli / 1000);
    milli = milli - (sec * 1000);
    struct timespec ts = {0};
    ts.tv_sec = sec;
    ts.tv_nsec = milli * 1000000L;
    if (nanosleep(&ts, &ts))
        ERR("nanosleep");
}

void print_keyboards_state(double* keyboards, int m, int k)
{
    for (int i=0;i<m;++i)
    {
        printf("Klawiatura nr %d:\n", i + 1);
        for (int j=0;j<k;++j)
            printf("  %e", keyboards[i * k + j]);
        printf("\n\n");
    }
}

void child_work(int m,int k, SharedData *shared){
    srand((unsigned)(time(NULL) ^ (unsigned long)getpid()));
    sem_t **sems = malloc(sizeof(sem_t) * m);
    if(sems == NULL)
        ERR("malloc failed");
    for(int j = 0;j < m;j++ ){
        char * name = get_semname(j);
        sems[j] = sem_open(name, O_CREAT, 0644, KEYBOARD_CAP);
        if(sems[j] == SEM_FAILED)
            ERR("sem open");

        free(name);
    }
    for(int j = 0; j < 10;j++){
        // keyboard  and  key indices 
        int keyboard_index = rand() % m;
        int key_index = rand() % k;

        // index for  mutex 
        int total_key_index = keyboard_index * k + key_index;

        sem_wait(sems[keyboard_index]);
        pthread_mutex_lock(&shared->keys_mutexes[total_key_index]);
        
        printf("PID<%d>: cleaning keyboard <%d> , key: %d \n",(pid_t)getpid(),(keyboard_index + 1), key_index);
        ms_sleep(300);

        // then divide the value of the corresponding field by 3
        shared->keys_state[total_key_index] /= 3.0;

        pthread_mutex_unlock(&shared->keys_mutexes[total_key_index]);
        sem_post(sems[keyboard_index]);
    }

    
    for(int j = 0; j<m;j++){
        sem_close(sems[j]);
    }
    free(sems);

    if ((rand() % 100) < 3) {
        fprintf(stderr, "[worker %d] simulating crash (abort)\n", getpid());
        abort();
    }

    munmap(shared, sizeof(SharedData));
    exit(EXIT_SUCCESS);
}

    int create_workers(pid_t *workers,int n, int m, int k, SharedData *shared){
    for(int i = 0; i < n;i++){
        workers[i] = fork();
        if(workers[i] == -1){
            perror("fork failed");
            return -1;
        }
        if(workers[i] == 0){
            pthread_barrier_wait(&shared->barrier);
            child_work(m,k,shared);
            exit(EXIT_SUCCESS);
        }
    }
    return 0;
}

static int wait_for_child(int n, const pid_t pids[])
{
    int all_ok = 1;

    for (int i = 0; i < n; i++) {
        int status;
        //wait for  processes 
        pid_t finished = waitpid(pids[i], &status, 0);

        if (finished == -1) { 
            perror("waitpid");
            all_ok = 0;
            continue;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
            fprintf(stderr, "[parent] worker %d (pid %d) terminated abnormally\n", i, pids[i]);
            all_ok = 0;
        }
    }
    return all_ok;
}

void remove_semafores(int m){
    for(int i = 0; i < m;i++){
        char* name = get_semname(i);
        sem_unlink(name);
        free(name);
    }
}

int parse_args(int argc, char **argv, int *n, int *m, int *k)
{
    if(argc != 4){
        usage("PolishTask");
        return -1;
    }
    *n = atoi(argv[1]);
    if(*n < KEYBOARD_CAP || *n > 20){
        usage("PolishTask");
        return -1;
    }
    *m = atoi(argv[2]);
    if(*m < 1 || *m > 5){
        usage("PolishTask");
        return -1;
    }
    *k = atoi(argv[3]);
    if(*k < 5 || *k > KEYBOARD_CAP){
        usage("PolishTask");
        return -1;
    }
    return 0;
}

int main(int argc, char** argv) 
{ 
    remove_semafores(MAX_KEYBOARDS);

    int n,m,k;
    if(parse_args(argc, argv, &n,&m,&k) == -1)
        exit(EXIT_FAILURE);

    pid_t *workers = (pid_t*)malloc(sizeof(pid_t) * n);
    if(workers == NULL){
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    // part 2 
    // pthread_barrier_t *barrier = mmap(NULL,sizeof(pthread_barrier_t),PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    // if(barrier == MAP_FAILED)
    //     ERR(mmap);

    // part 2 
    // pthread_barrierattr_t attr;
    // pthread_barrierattr_init(&attr);
    // pthread_barrierattr_setpshared(&attr,PTHREAD_PROCESS_SHARED);
    // if(pthread_barrier_init(barrier, &attr, n+1)!=0)
    //     ERR("couldn't initialize barrier");
    // pthread_barrierattr_destroy(&attr);

    // if(create_workers(workers, n, m,k, barrier) == -1)
    //     ERR("couldn't create processes");
    
    // rodzic  tworzy i spokojnie  tworzy 
    // chcemy size dla wiec liczymi ile czego jest  
    //use a separate mutex for each key.
    size_t rozmiar = sizeof(SharedData);

    shm_unlink(SHARED_MEM_NAME);

    int fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0644);
    if(ftruncate(fd, rozmiar) == -1)
        ERR("ftruncate");


    SharedData* shared = mmap(NULL, rozmiar, PROT_READ | PROT_WRITE , MAP_SHARED , fd ,0);
    if(shared == NULL)
        ERR("mmap");
    close(fd);

    // barrier  init  for  part3 
    pthread_barrierattr_t attr;
    pthread_barrierattr_init(&attr);
    pthread_barrierattr_setpshared(&attr,PTHREAD_PROCESS_SHARED);
    if(pthread_barrier_init(&shared->barrier, &attr, n+1)!=0)
        ERR("couldn't initialize barrier");
    pthread_barrierattr_destroy(&attr);

    // mutex  init  for  part  3  

    pthread_mutexattr_t m_attr;
    pthread_mutexattr_init(&m_attr);
    pthread_mutexattr_setpshared(&m_attr, PTHREAD_PROCESS_SHARED);

    for(int i = 0; i < m * k; i++){
        shared->keys_state[i] = 1.0;
        pthread_mutex_init(&shared->keys_mutexes[i],&m_attr);
    }
    pthread_mutexattr_destroy(&m_attr);



    if(create_workers(workers, n, m,k, shared) == -1)
        ERR("couldn't create processes");


    ms_sleep(500);
    pthread_barrier_wait(&shared->barrier);

    int all_ok = wait_for_child(n, workers);

    if (!all_ok) {
        printf("Computation failed!!!\n");
    } else {
        printf("Cleaning finished! Final keyboards state:\n");
        print_keyboards_state(shared->keys_state, m, k);
    }
    
    
    //printf("Cleaning finished! \n");
    
    remove_semafores(m);    

    pthread_barrier_destroy(&shared->barrier);
    for(int i = 0 ; i < m*k ; i++)
        pthread_mutex_destroy(&shared->keys_mutexes[i]);
    
    // zazmiana  teraz  inne mapowanie  juz  nie  
    //munmap(&shared->barrier, sizeof(pthread_barrier_t)); 

    free(workers);
    // tylko 
    munmap(shared, sizeof(SharedData));
    shm_unlink(SHARED_MEM_NAME);
    exit(EXIT_SUCCESS);
}