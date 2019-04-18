#include <stdio.h> 
#include <stdlib.h> 
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <semaphore.h>

#define log(msg) do{printf("LOG: %s\n", msg);}while(0)
#define elog(msg) do{printf("ERROR LOG: %s\n", msg);}while(0)
#define msg(caller, msg) do{printf("%s: %s\n", caller, msg);}while(0)

#ifdef NDEBUG
#define log(msg) do{;}while(0)
#define elog(msg) do{;}while(0)
#define msg(caller, msg) do{;}while(0)
#endif

#define errMsg(msg) do{printf("ERROR: %s\n", msg);}while(0)

//types
#define HACK 0
#define SERF 1
#define TYPE_LEN 4

//actions
enum action {spawn, pierAccess, board};
typedef enum action action_t;

typedef struct Args{
    int P; //number of procceses of each category to be generated
    int H; //max time after which a new hacker will be initialized
    int S; //max time after which a new serf will be initialized
    int R; //max time of boat ride
    int W; //max wait until space on pier time
    int C; //pier capacity
}args_t;

typedef struct shm_sem{
    sem_t *semIO;
    
    int *actionCounter;
    sem_t *semActionCounter;

    int *hackCounter;
    sem_t *semHackCounter;

    int *serfCounter;
    sem_t *semSerfCounter;

    int *hacksOnPier;
    sem_t *semHacksOnPier;

    int *serfsOnPier;
    sem_t *semSerfsOnPier;
}shm_sem_t;

int mainWrapper(int argc, char* argv[]);
int parseArgs(int argc, char* argv[], args_t *args);
int generatePersons(int type, args_t *args, shm_sem_t *shared);
int performActions(int type, args_t *args, shm_sem_t *shared);
int output(int type, action_t action, args_t *args, shm_sem_t *shared, int* id);

int main(int argc, char* argv[]){
    /*
    int shmFd = shm_open("/shared", O_CREAT | O_RDWR, 0666);
    ftruncate(shmFd, 1024);
    void* shmPtr = mmap(0, 1024, PROT_WRITE, MAP_SHARED, shmFd, 0);
    sprintf(shmPtr, "This was in shared memory before");

    if(fork() == 0){
        int shmFd = shm_open("/shared", O_RDWR, 0666);
        void* shmPtr = mmap(0, 1024, PROT_READ, MAP_SHARED, shmFd, 0);
        printf("I am child, and this is in shared memory: %s\n", (char*)shmPtr);
        
        shm_unlink("/shared");
        return 0;
    }else{
        sprintf(shmPtr, "Parent has overwriten the shared memory");
        printf("I am parent, and this is in shared memory: %s\n", (char*) shmPtr);
    }
    */
    srand(time(NULL)); //shuffle the RNG

    int ErrorNum=mainWrapper(argc, argv);

    switch (ErrorNum)
    {
        case 0:
            break;

        case 1:
            errMsg("Inccorect Number of arguments");
            break;

        case 2:
            errMsg("Inccorect format of arguments. All arguments should be integers");
            break;

        case 3:
            errMsg("Error while initializing semaphore");
            break;
    
        default:
            errMsg("!!!SOMENE FORGOT TO WRITE AN ERROR MESSAGE!!!");
    }

    return ErrorNum;
}

int mainWrapper(int argc, char* argv[]){
    /*argument handling*/
    args_t *args = malloc(sizeof(args_t));
    int argCode = parseArgs(argc, argv, args);
    if(argCode != 0){
        return argCode;
    }

    /*shared holder*/
    shm_sem_t *shared = malloc(sizeof(shm_sem_t));

    /*IO semaphore*/
    shared->semIO = (sem_t*)malloc(sizeof(sem_t));
    if(sem_init(shared->semIO, 1, 1) < 0){
        return 3; //Error while initializing semaphore
    }

    /*shared action counter*/
    int shmActionCounter = shm_open("/shmActionCounter", O_CREAT | O_RDWR, 0666);
    ftruncate(shmActionCounter, sizeof(int));
    shared->actionCounter = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmActionCounter, 0);
    *(shared->actionCounter) = 0; //action counter initialization
    shared->semActionCounter = (sem_t*)malloc(sizeof(sem_t)); //semaphore guarding the action counter
    if(sem_init(shared->semActionCounter, 1, 1) < 0){
        return 3; //Error while initializing semaphore
    }
    /*shared hacks on pier counter*/
    int shmHacksOnPier = shm_open("/shmHacksOnPier", O_CREAT | O_RDWR, 0666);
    ftruncate(shmHacksOnPier, sizeof(int));
    shared->hacksOnPier = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmHacksOnPier, 0);
    *(shared->hacksOnPier) = 0; //action counter initialization
    shared->semHacksOnPier = (sem_t*)malloc(sizeof(sem_t)); //semaphore guarding the action counter
    if(sem_init(shared->semHacksOnPier, 1, 1) < 0){
        return 3; //Error while initializing semaphore
    }
    /*shared serfs on pier counter*/
    int shmSerfsOnPier = shm_open("/shmSerfsOnPier", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSerfsOnPier, sizeof(int));
    shared->serfsOnPier = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmSerfsOnPier, 0);
    *(shared->serfsOnPier) = 0; //action counter initialization
    shared->semSerfsOnPier = (sem_t*)malloc(sizeof(sem_t)); //semaphore guarding the action counter
    if(sem_init(shared->semSerfsOnPier, 1, 1) < 0){
        return 3; //Error while initializing semaphore
    }

    int status1 = 0;
    int status2 = 0;
    /*start the generators*/
    int pid1 = fork(); //fork the hacker generator
    if(pid1 == 0){
        sem_wait(shared->semIO);
        log("I'm Hacker generator!");
        sem_post(shared->semIO);
        generatePersons(HACK, args, shared);
        exit(0);
    }else if(pid1 > 0){
        int pid2 = fork(); //fork the serf generator
        if(pid2 == 0){
            sem_wait(shared->semIO);
            log("I'm Serf generator!");
            sem_post(shared->semIO);
            generatePersons(SERF, args, shared);
            exit(0);
        }

        sem_wait(shared->semIO);
        msg("Parent", "Waiting for generators to exit");
        sem_post(shared->semIO);
        int exitPid = 0;
        exitPid = waitpid(0, &status1, 0);
        sem_wait(shared->semIO);
        printf("Parent: Generator %d has exited\n", exitPid);
        sem_post(shared->semIO);
        exitPid = waitpid(0, &status2, 0);
        sem_wait(shared->semIO);
        printf("Parent: Generator %d has exited\n", exitPid);
        sem_post(shared->semIO);


        //clean up
        munmap(shared->actionCounter, sizeof(int));
        munmap(shared->hacksOnPier, sizeof(int));
        munmap(shared->serfsOnPier, sizeof(int));
        sem_destroy(shared->semActionCounter);
        sem_destroy(shared->semSerfsOnPier);
        sem_destroy(shared->semHacksOnPier);
        sem_destroy(shared->semIO);

        exit(0);
    }
    return -1;
}

int generatePersons(int type, args_t *args, shm_sem_t *shared){
    //get the spawn interval
    int spawnTime = 0;
    if(type == HACK){
        spawnTime = args->H;
    }else{
        spawnTime = args->S;
    }
    spawnTime *= 1000; //convert to microseconds for use with usleep()
    
    //prepare an array for holding PIDs
    int* pids = malloc(sizeof(int) * args->P);

    /*persons counter*/
    if(type == HACK){
        int shmHackCounter = shm_open("/shmHackCounter", O_CREAT | O_RDWR, 0666);
        ftruncate(shmHackCounter, sizeof(int));
        shared->hackCounter = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmHackCounter, 0);
        *(shared->hackCounter) = 0; //persons counter initialization

        shared->semHackCounter = (sem_t*)malloc(sizeof(sem_t)); //semaphore guarding the persons counter
        if(sem_init(shared->semHackCounter, 1, 1) < 0){
            return 3; //Error while initializing semaphore
        }
    }else{
        int shmSerfCounter = shm_open("/shmSerfCounter", O_CREAT | O_RDWR, 0666);
        ftruncate(shmSerfCounter, sizeof(int));
        shared->serfCounter = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmSerfCounter, 0);
        *(shared->serfCounter) = 0; //persons counter initialization

        shared->semSerfCounter = (sem_t*)malloc(sizeof(sem_t)); //semaphore guarding the persons counter
        if(sem_init(shared->semSerfCounter, 1, 1) < 0){
            return 3; //Error while initializing semaphore
        }
    }
    

    //fork P new persons
    for(size_t i = 0; i < args->P; i++)
    {
        int r = rand() % (spawnTime+1); //determine the spawn time for this person
        usleep(r);
        pids[i] = fork();
        if(pids[i] == 0){

            performActions(type, args, shared);

            exit(0);
        }
    }
    for(size_t i = 0; i < args->P; i++){
        waitpid(pids[i], NULL, 0);
    }

    //clean up
    free(pids);
    munmap(shared->hackCounter, sizeof(int));
    munmap(shared->serfCounter, sizeof(int));
    sem_destroy(shared->semHackCounter);
    sem_destroy(shared->semSerfCounter);
    
    return 0;
}

int performActions(int type, args_t *args, shm_sem_t *shared){
    //Person spawned
    int id = 0;
    output(type, spawn, args, shared, &id);

    //Attempts to access the pier
    output(type, pierAccess, args, shared, &id);

    //Attempts to bard the boat
    output(type, board, args, shared, &id);


    return 0;
}

int output(int type, action_t action, args_t *args, shm_sem_t *shared, int* id){
    int ret = 0; //value to be returned

    //modify for type
    char* typeStr = (char*)malloc(TYPE_LEN+1);
    int *personsCounter;
    int *personsOnPier;
    int *otherPersonsOnPier;
    sem_t *semPersonsCounter;
    sem_t *semPersonsOnPier;
    sem_t *semOtherPersonsOnPier;
    if(type == HACK){
        strcpy(typeStr, "HACK");
        personsCounter = shared->hackCounter;
        semPersonsCounter = shared->semHackCounter;
        personsOnPier = shared->hacksOnPier;
        semPersonsOnPier = shared->semHacksOnPier;
        otherPersonsOnPier = shared->serfsOnPier;
        semOtherPersonsOnPier = shared->semSerfsOnPier;
    }else{
        strcpy(typeStr, "SERF");
        personsCounter = shared->serfCounter;
        semPersonsCounter = shared->semSerfCounter;
        personsOnPier = shared->serfsOnPier;
        semPersonsOnPier = shared->semSerfsOnPier;
        otherPersonsOnPier = shared->hacksOnPier;
        semOtherPersonsOnPier = shared->semHacksOnPier;
    }

    //preform action
    switch (action)
    {
        case spawn:
            sem_wait(shared->semIO);
            sem_wait(shared->semActionCounter);
            sem_wait(semPersonsCounter);
            *id = *(personsCounter);
            printf("%d: %s %d: starts\n", *(shared->actionCounter), typeStr, *id);
            sem_post(shared->semIO);
            (*(shared->actionCounter))++;
            sem_post(shared->semActionCounter);
            (*(personsCounter))++;
            sem_post(semPersonsCounter);
            break;

        case pierAccess:
            while(true){
                if((*(shared->serfsOnPier) + *(shared->hacksOnPier)) < args->C){ //there's room
                    sem_wait(shared->semIO);
                    sem_wait(shared->semActionCounter);
                    sem_wait(semPersonsOnPier);
                    (*(personsOnPier))++;
                    printf("%d: %s %d: waits: %d: %d.\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                    sem_post(shared->semIO);
                    sem_post(semPersonsOnPier);
                    (*(shared->actionCounter))++;
                    sem_post(shared->semActionCounter);
                    break;
                }else{
                    sem_wait(shared->semIO);
                    sem_wait(shared->semActionCounter);
                    sem_wait(semPersonsOnPier);
                    printf("%d: %s %d: leaves queue: %d: %d.\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                    sem_post(shared->semIO);
                    (*(shared->actionCounter))++;
                    sem_post(shared->semActionCounter);
                    sem_post(semPersonsOnPier);
                    
                    //go to sleep
                    int r = rand() % ((args->W)*1000+1)+20*1000;
                    usleep(r);

                    sem_wait(shared->semIO);
                    sem_wait(shared->semActionCounter);
                    printf("%d: %s %d: is back\n", *(shared->actionCounter), typeStr, *id);
                    sem_post(shared->semIO);
                    (*(shared->actionCounter))++;
                    sem_post(shared->semActionCounter);
                    exit(0);
                }
            }
            break;
        
        case board:
            if(*personsOnPier == 4){
                
            }else if(*personsOnPier == 2 && *otherPersonsOnPier >= 2){

            }else{

            }
            
            break;

        default:
            break;
    }

    return ret;
}

int parseArgs(int argc, char* argv[], args_t *args){
    if(argc != 7){
        return 1; //incorrect num of arguments
    }
    
    char* pEnd;

    //number of procceses of each category to be generated
    int P = strtol(argv[1], &pEnd, 10);
    if(*pEnd != '\0'){
        return 2;
    }
    if(P < 2 || (P % 2) != 0){
        return 2;
    }

    //max time after which a new hacker will be initialized
    int H = strtol(argv[2], &pEnd, 10);
    if(*pEnd != '\0'){
        return 2;
    }
    if(H < 0 || H > 2000){
        return 2;
    }

    //max time after which a new serf will be initialized
    int S = strtol(argv[3], &pEnd, 10);
    if(*pEnd != '\0'){
        return 2;
    }
    if(S < 0 || H > 2000){
        return 2;
    }

    //max time of boat ride
    int R = strtol(argv[4], &pEnd, 10); 
    if(*pEnd != '\0'){
        return 2;
    }
    if(R < 0 || R > 2000){
        return 2;
    }

    //max wait until space on pier time
    int W = strtol(argv[5], &pEnd, 10);
    if(*pEnd != '\0'){
        return 2;
    }
    if(W < 20 || W > 2000){
        return 2;
    }

    //pier capacity
    int C = strtol(argv[6], &pEnd, 10);
    if(*pEnd != '\0'){
        return 2;
    }
    if(C < 5){
        return 2;
    }

    //fill args structure
    args->P = P;
    args->H = H;
    args->S = S;
    args->R = R;
    args->W = W;
    args->C = C;

    return 0;
}