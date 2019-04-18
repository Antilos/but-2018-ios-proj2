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
#define dprintf(...) do{printf(__VA_ARGS__);}while(0)

#ifdef NDEBUG
#define log(msg) do{;}while(0)
#define elog(msg) do{;}while(0)
#define msg(caller, msg) do{;}while(0)
#define dprintf(str, ...) do{;}while(0)
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
    sem_t *mutex;
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

    //for synchronizing boarding
    int boatCapacity;
    int *boatCounter;
    int *hacksOnBoat;
    int *serfsOnBoat;
    sem_t *semTurnstile1;
    sem_t *semTurnstile2;
    sem_t *captainsMutex;
}shm_sem_t;

int mainWrapper(int argc, char* argv[]);
int parseArgs(int argc, char* argv[], args_t *args);
int generatePersons(int type, args_t *args, shm_sem_t *shared);
int performActions(int type, args_t *args, shm_sem_t *shared);
int output(int type, action_t action, args_t *args, shm_sem_t *shared, int* id);
bool canBoard(int type, shm_sem_t *shared);

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
    shared->boatCapacity = 4; //initialize boat capacity

    /*shared mutex*/ //needs to be shared, as second generation children need to see it
    int shmMutex = shm_open("/shmMutex", O_CREAT | O_RDWR, 0666);
    ftruncate(shmMutex, sizeof(sem_t));
    shared->mutex = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmMutex, 0);
    if(sem_init(shared->mutex, 1, 1) < 0){
        return 3; //Error while initializing semaphore
    }

    /*IO semaphore*/
    shared->semIO = (sem_t*)malloc(sizeof(sem_t));
    if(sem_init(shared->semIO, 1, 1) < 0){
        return 3; //Error while initializing semaphore
    }

    /*shared action counter*/
    int shmActionCounter = shm_open("/shmActionCounter", O_CREAT | O_RDWR, 0666);
    ftruncate(shmActionCounter, sizeof(int));
    shared->actionCounter = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmActionCounter, 0);
    *(shared->actionCounter) = 1; //initialization
    shared->semActionCounter = (sem_t*)malloc(sizeof(sem_t)); //semaphore guarding the action counter
    if(sem_init(shared->semActionCounter, 1, 1) < 0){
        return 3; //Error while initializing semaphore
    }
    /*shared hacks on pier counter*/
    int shmHacksOnPier = shm_open("/shmHacksOnPier", O_CREAT | O_RDWR, 0666);
    ftruncate(shmHacksOnPier, sizeof(int));
    shared->hacksOnPier = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmHacksOnPier, 0);
    *(shared->hacksOnPier) = 0; //initialization
    shared->semHacksOnPier = (sem_t*)malloc(sizeof(sem_t)); //semaphore guarding the action counter
    if(sem_init(shared->semHacksOnPier, 1, 1) < 0){
        return 3; //Error while initializing semaphore
    }
    /*shared serfs on pier counter*/
    int shmSerfsOnPier = shm_open("/shmSerfsOnPier", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSerfsOnPier, sizeof(int));
    shared->serfsOnPier = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmSerfsOnPier, 0);
    *(shared->serfsOnPier) = 0; //initialization
    shared->semSerfsOnPier = (sem_t*)malloc(sizeof(sem_t)); //semaphore guarding the action counter
    if(sem_init(shared->semSerfsOnPier, 1, 1) < 0){
        return 3; //Error while initializing semaphore
    }

     /*shared boat counter*/
    int shmBoatCounter = shm_open("/shmBoatCounter", O_CREAT | O_RDWR, 0666);
    ftruncate(shmBoatCounter, sizeof(int));
    shared->boatCounter = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmBoatCounter, 0);
    *(shared->boatCounter) = 0; //initialization

    /*shared hacks on boat counter*/
    int shmHacksOnBoat = shm_open("/shmHacksOnBoat", O_CREAT | O_RDWR, 0666);
    ftruncate(shmHacksOnBoat, sizeof(int));
    shared->hacksOnBoat = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmHacksOnBoat, 0);
    *(shared->hacksOnBoat) = 0; //initialization

    /*shared serfs on boat counter*/
    int shmSerfsOnBoat = shm_open("/shmSerfsOnBoat", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSerfsOnBoat, sizeof(int));
    shared->serfsOnBoat = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmSerfsOnBoat, 0);
    *(shared->serfsOnBoat) = 0; //initialization

    //turnstile semaphores for barrier implementation (double randezvouse)
    int shmTurnstile1 = shm_open("/shmTurnstile1", O_CREAT | O_RDWR, 0666);
    ftruncate(shmTurnstile1, sizeof(sem_t));
    shared->semTurnstile1 = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmTurnstile1, 0); //bars entry to crit section untill all processes have arrived
    if(sem_init(shared->semTurnstile1, 1, 0) < 0){ //locked
        return 3; //Error while initializing semaphore
    }

    int shmTurnstile2 = shm_open("/shmTurnstile2", O_CREAT | O_RDWR, 0666);
    ftruncate(shmTurnstile2, sizeof(sem_t));
    shared->semTurnstile2 = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmTurnstile2, 0);//makes processes wait until all other processes have finished crit section
    if(sem_init(shared->semTurnstile2, 1, 1) < 0){ //unlocked
        return 3; //Error while initializing semaphore
    }

     /*mutex for when a process is checking if whether it can become captain*/
    int shmCaptainsMutex = shm_open("/shmCaptainsMutex", O_CREAT | O_RDWR, 0666);
    ftruncate(shmCaptainsMutex, sizeof(sem_t));
    shared->captainsMutex = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmCaptainsMutex, 0);
    if(sem_init(shared->captainsMutex, 1, 1) < 0){
        return 3; //Error while initializing semaphore
    }

    int status1 = 0;
    int status2 = 0;
    /*start the generators*/
    int pid1 = fork(); //fork the hacker generator
    if(pid1 == 0){
        sem_wait(shared->mutex);
        log("I'm Hacker generator!");
        sem_post(shared->mutex);
        generatePersons(HACK, args, shared);
        exit(0);
    }else if(pid1 > 0){
        int pid2 = fork(); //fork the serf generator
        if(pid2 == 0){
            sem_wait(shared->mutex);
            log("I'm Serf generator!");
            sem_post(shared->mutex);
            generatePersons(SERF, args, shared);
            exit(0);
        }

        sem_wait(shared->mutex);
        msg("Parent", "Waiting for generators to exit");
        sem_post(shared->mutex);
        int exitPid = 0;
        exitPid = waitpid(0, &status1, 0);
        sem_wait(shared->mutex);
        printf("Parent: Generator %d has exited\n", exitPid);
        sem_post(shared->mutex);
        exitPid = waitpid(0, &status2, 0);
        sem_wait(shared->mutex);
        printf("Parent: Generator %d has exited\n", exitPid);
        sem_post(shared->mutex);


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

    //Attempts to board the boat
    while(output(type, board, args, shared, &id)); //should return 1 if process didn't manage to board


    return 0;
}

int output(int type, action_t action, args_t *args, shm_sem_t *shared, int* id){
    int ret = 0; //value to be returned

    bool isCaptain = false;

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
            sem_wait(shared->mutex);
            *id = *(personsCounter);
            printf("%d: %s %d: starts\n", *(shared->actionCounter), typeStr, *id);
            (*(shared->actionCounter))++;
            (*(personsCounter))++;
            sem_post(shared->mutex);
            break;

        case pierAccess:
            while(true){
                if((*(shared->serfsOnPier) + *(shared->hacksOnPier)) < args->C){ //there's room
                    sem_wait(shared->mutex);
                    (*(personsOnPier))++;
                    printf("%d: %s %d: waits: %d: %d.\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                    (*(shared->actionCounter))++;
                    sem_post(shared->mutex);
                    break;
                }else{
                    sem_wait(shared->mutex);
                    printf("%d: %s %d: leaves queue: %d: %d.\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                    (*(shared->actionCounter))++;
                    sem_post(shared->mutex);

                    //go to sleep
                    int r = rand() % ((args->W)*1000+1)+20*1000;
                    usleep(r);

                    sem_wait(shared->mutex);
                    printf("%d: %s %d: is back\n", *(shared->actionCounter), typeStr, *id);
                    (*(shared->actionCounter))++;
                    sem_post(shared->mutex);
                    //exit(0);
                }
            }
            break;
        
        case board:
            sem_wait(shared->captainsMutex);
            dprintf("--DING: Tries to board\n");
            //check if there is enough people to form crew
            if(*personsOnPier == 4){
                (*(personsOnPier))=0;
                isCaptain = true;
            }else if(*personsOnPier == 2 && *otherPersonsOnPier >= 2){
                (*(personsOnPier))=0;
                (*(otherPersonsOnPier))-=2;
                isCaptain = true;
            }else{
                sem_post(shared->captainsMutex);
                //return 1; //didn't manage to board
            }

            //ALL ABOARD!!!
            sem_wait(shared->mutex);
            if(!canBoard(type, shared)){ //this person can't board
                sem_post(shared->mutex);
                return 1; //didn't manage to board
            }

            if(isCaptain){
                printf("%d: %s %d: boards: %d: %d\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                (*(shared->actionCounter))++;
            }

            //barrier first phase
            (*(shared->boatCounter)) += 1;
            if((*(shared->boatCounter)) == shared->boatCapacity){
                sem_wait(shared->semTurnstile2);
                sem_post(shared->semTurnstile1);
            }
            sem_post(shared->mutex);
            
            sem_wait(shared->mutex);
            dprintf("-- %s %d: waiting for all to board boatCounter: %d\n", typeStr, *id, *(shared->boatCounter));
            sem_post(shared->mutex);

            //wait for the boat to be fully boarded
            sem_wait(shared->semTurnstile1);
            sem_post(shared->semTurnstile1);
            sem_wait(shared->mutex);
            dprintf("-- %s %d: All boarded boatCounter: %d\n", typeStr, *id, *(shared->boatCounter));
            sem_post(shared->mutex);
            
            //SAIL!!!

            if(isCaptain){
                int r = rand() % ((args->R)*1000+1)+20*1000;
                usleep(r);
                sem_post(shared->captainsMutex);
            }else{
                sem_wait(shared->mutex);
                dprintf("-- %s %d: waiting for Captain board boatCounter: %d\n", typeStr, *id, *(shared->boatCounter));
                sem_post(shared->mutex);
                sem_wait(shared->captainsMutex);
                sem_post(shared->captainsMutex);
                dprintf("-- %s %d: Boat ride done boatCounter: %d\n", typeStr, *id, *(shared->boatCounter));
            }

            //barrier second phase
            sem_wait(shared->mutex);
            (*(shared->boatCounter)) -= 1;
            if((*(shared->boatCounter)) == 0){
                sem_wait(shared->semTurnstile1);
                sem_post(shared->semTurnstile2);
            }
            sem_post(shared->mutex);

            sem_wait(shared->mutex);
            dprintf("-- %s %d: waiting for everyone to get off boatCounter: %d\n", typeStr, *id, *(shared->boatCounter));
            sem_post(shared->mutex);
            //wait for everyone to get off the boat
            sem_wait(shared->semTurnstile2);
            sem_post(shared->semTurnstile2);

            sem_wait(shared->mutex);
            if(isCaptain){
                printf("%d: %s %d: captain exits: %d: %d\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                (*(shared->actionCounter))++;
            }else{
                printf("%d: %s %d: member exits: %d: %d\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                (*(shared->actionCounter))++;
            }
            sem_post(shared->mutex);
            return 0;
            
            break;

        default:
            break;
    }

    return ret;
}

/**
 * @brief Decides whether the particullar process can board the ship
 * 
 * @param type Type of the process: HACK(0) or SERF(1)
 * @param shared Pointer to a wrapper structure for accesing shared memory and semaphores
 * @return true Can board
 * @return false Can't board
 */
bool canBoard(int type, shm_sem_t *shared){
    switch (type)
    {
        case HACK: //hacks
            if(*(shared->boatCounter) == 3 && *(shared->serfsOnBoat) == 1){
                return false;
            }else if(*(shared->boatCounter) == 3 && *(shared->serfsOnBoat) == 3){
                return false;
            }else{
                return true;
            }
            break;
    
        case SERF: //serfs
            if(*(shared->boatCounter) == 3 && *(shared->hacksOnBoat) == 1){
                return false;
            }else if(*(shared->boatCounter) == 3 && *(shared->hacksOnBoat) == 3){
                return false;
            }else{
                return true;
            }
            break;

        default:
            return true;
            break;
    }
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