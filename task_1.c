#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define MONTE_CARLO_ITERS 100000
#define LOG_LEN 8

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))




void usage(){
    printf("n - number of proceses[1,29]");
}
int main(int argc, char** argv){
    if(argc != 2)usage();
    int n = atoi(argv[1]);
    if(n < 0 || n > 30)usage();

    int log_fd;
    if(log_fd = open("log.txt", O_CREAT | O_RDWR | O_TRUNC, 0644) == -1)   // try to change the permitions to -1 
        ERR("Couldn't open file");
    if((ftruncate(log_fd, LOG_LEN *n)) == -1)
        ERR("Couldn't initialize size");

    char* log;
    if(log = (char*)mmap(NULL, n*LOG_LEN, PROT_WRITE | PROT_READ, MAP_SHARED, log_fd, -1) == -1)
        ERR("Mapping failed");
    float* data;
    if(data = (float*)mmap(NULL, n*sizeof(float), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS,-1,0) == -1) // czemu -1 
        ERR("error ");

    for(int i = 0; i<n;i++){
        pid_t pid = fork();
        if(pid == -1)
            ERR("fork failed");
        // else if(pid == 0)
        //     //child  work 
        // else{
        //     //parent work
        // }
    }
    


}