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

#ifdef DEBUG
#define log(msg) do{printf("LOG: %s\n", msg);}while(0)
#define elog(msg) do{printf("ERROR LOG: %s\n", msg);}while(0)
#define msg(caller, msg) do{printf("%s: %s\n", caller, msg);}while(0)
#define dprintf(...) do{printf(__VA_ARGS__);}while(0)
#endif

#ifndef DEBUG
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
    FILE* fout; //output file

    sem_t *mutex;
    sem_t *semIO;
    
    int *actionCounter;
    sem_t *semActionCounter;

    int *hackCounter;
    sem_t *semHackCounter;
    int *serfCounter;
    sem_t *semSerfCounter;

    int *hacksOnPier;
    int *serfsOnPier;


    //for synchronizing boarding
    int *hacksWaitingToBoard; //same as number of persons waiting on in Queue
    int *serfsWaitingToBoard; //same as number of persons waiting on in Queue
    sem_t *semHacksOnPierQueue;
    sem_t *semSerfsOnPierQueue;
    int boatCapacity;
    int *boatCounter;
    int *hacksOnBoat;
    int *serfsOnBoat;
    sem_t *semTurnstile1;
    sem_t *semTurnstile2;
    sem_t *captainsMutex;
    sem_t *semBoatRide;
    int *membersStillToLeave;
    sem_t *semCaptainCanLeave;
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
    close(shmMutex);
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
    close(shmActionCounter);
    *(shared->actionCounter) = 1; //initialization
    shared->semActionCounter = (sem_t*)malloc(sizeof(sem_t)); //semaphore guarding the action counter
    if(sem_init(shared->semActionCounter, 1, 1) < 0){
        return 3; //Error while initializing semaphore
    }
    /*shared hacks on pier counter*/
    int shmHacksOnPier = shm_open("/shmHacksOnPier", O_CREAT | O_RDWR, 0666);
    ftruncate(shmHacksOnPier, sizeof(int));
    shared->hacksOnPier = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmHacksOnPier, 0);
    close(shmHacksOnPier);
    *(shared->hacksOnPier) = 0; //initialization
    
    /*shared serfs on pier counter*/
    int shmSerfsOnPier = shm_open("/shmSerfsOnPier", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSerfsOnPier, sizeof(int));
    shared->serfsOnPier = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmSerfsOnPier, 0);
    close(shmSerfsOnPier);
    *(shared->serfsOnPier) = 0; //initialization

     /*shared boat counter*/
    int shmBoatCounter = shm_open("/shmBoatCounter", O_CREAT | O_RDWR, 0666);
    ftruncate(shmBoatCounter, sizeof(int));
    shared->boatCounter = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmBoatCounter, 0);
    close(shmBoatCounter);
    *(shared->boatCounter) = 0; //initialization

    /*shared hacks in queue counter*/
    int shmHacksWaitingToBoard = shm_open("/shmHacksWaitingToBoard", O_CREAT | O_RDWR, 0666);
    ftruncate(shmHacksWaitingToBoard, sizeof(int));
    shared->hacksWaitingToBoard = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmHacksWaitingToBoard, 0);
    close(shmHacksWaitingToBoard);
    *(shared->hacksWaitingToBoard) = 0; //initialization

    /*shared serfs in queue counter*/
    int shmSerfsWaitingToBoard = shm_open("/shmSerfsWaitingToBoard", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSerfsWaitingToBoard, sizeof(int));
    shared->serfsWaitingToBoard = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmSerfsWaitingToBoard, 0);
    close(shmSerfsWaitingToBoard);
    *(shared->serfsWaitingToBoard) = 0; //initialization

    /*HacksOnPierQueue*/
    int shmHacksOnPierQueue = shm_open("/shmHacksOnPierQueue", O_CREAT | O_RDWR, 0666);
    ftruncate(shmHacksOnPierQueue, sizeof(sem_t));
    shared->semHacksOnPierQueue = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmHacksOnPierQueue, 0);
    close(shmHacksOnPierQueue);
    if(sem_init(shared->semHacksOnPierQueue, 1, 0) < 0){
        return 3; //Error while initializing semaphore
    }

    /*SerfsOnPierQueue*/
    int shmSerfsOnPierQueue = shm_open("/shmSerfsOnPierQueue", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSerfsOnPierQueue, sizeof(sem_t));
    shared->semSerfsOnPierQueue = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmSerfsOnPierQueue, 0);
    close(shmSerfsOnPierQueue);
    if(sem_init(shared->semSerfsOnPierQueue, 1, 0) < 0){
        return 3; //Error while initializing semaphore
    }

    /*shared hacks on boat counter*/
    int shmHacksOnBoat = shm_open("/shmHacksOnBoat", O_CREAT | O_RDWR, 0666);
    ftruncate(shmHacksOnBoat, sizeof(int));
    shared->hacksOnBoat = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmHacksOnBoat, 0);
    close(shmHacksOnBoat);
    *(shared->hacksOnBoat) = 0; //initialization

    /*shared serfs on boat counter*/
    int shmSerfsOnBoat = shm_open("/shmSerfsOnBoat", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSerfsOnBoat, sizeof(int));
    shared->serfsOnBoat = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmSerfsOnBoat, 0);
    close(shmSerfsOnBoat);
    *(shared->serfsOnBoat) = 0; //initialization

    //turnstile semaphores for barrier implementation (double randezvouse)
    int shmTurnstile1 = shm_open("/shmTurnstile1", O_CREAT | O_RDWR, 0666);
    ftruncate(shmTurnstile1, sizeof(sem_t));
    shared->semTurnstile1 = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmTurnstile1, 0); //bars entry to crit section untill all processes have arrived
    close(shmTurnstile1);
    if(sem_init(shared->semTurnstile1, 1, 0) < 0){ //locked
        return 3; //Error while initializing semaphore
    }

    int shmTurnstile2 = shm_open("/shmTurnstile2", O_CREAT | O_RDWR, 0666);
    ftruncate(shmTurnstile2, sizeof(sem_t));
    shared->semTurnstile2 = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmTurnstile2, 0);//makes processes wait until all other processes have finished crit section
    close(shmTurnstile2);
    if(sem_init(shared->semTurnstile2, 1, 1) < 0){ //unlocked
        return 3; //Error while initializing semaphore
    }

     /*mutex for when a process is checking if whether it can become captain*/
    int shmCaptainsMutex = shm_open("/shmCaptainsMutex", O_CREAT | O_RDWR, 0666);
    ftruncate(shmCaptainsMutex, sizeof(sem_t));
    shared->captainsMutex = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmCaptainsMutex, 0);
    close(shmCaptainsMutex);
    if(sem_init(shared->captainsMutex, 1, 1) < 0){
        return 3; //Error while initializing semaphore
    }

    /*Semaphore to keep persons from exiting the boat before the ride is over*/
    int shmSemBoatRide = shm_open("/shmSemBoatRide", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSemBoatRide, sizeof(sem_t));
    shared->semBoatRide = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmSemBoatRide, 0);
    close(shmSemBoatRide);
    if(sem_init(shared->semBoatRide, 1, 0) < 0){
        return 3; //Error while initializing semaphore
    }

    /*Semaphore to keep the captain from leaving before all members are gone*/
    int shmSemCaptainCanLeave = shm_open("/shmSemCaptainCanLeave", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSemCaptainCanLeave, sizeof(sem_t));
    shared->semCaptainCanLeave = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmSemCaptainCanLeave, 0);
    close(shmSemCaptainCanLeave);
    if(sem_init(shared->semCaptainCanLeave, 1, 0) < 0){
        return 3; //Error while initializing semaphore
    }

    /*shared serfs on boat counter*/
    int shmMembersStillToLeave = shm_open("/shmMembersStillToLeave", O_CREAT | O_RDWR, 0666);
    ftruncate(shmMembersStillToLeave, sizeof(int));
    shared->membersStillToLeave = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmMembersStillToLeave, 0);
    close(shmMembersStillToLeave);
    *(shared->membersStillToLeave) = 3; //initialization (3, since boat capacity is 4)

    /*open output file*/
    shared->fout = fopen("proj2.out", "W");
    //freopen("proj2.out", "W", stdout);

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
        //sem_wait(shared->mutex);
        //printf("Parent: Generator %d has exited\n", exitPid);
        //sem_post(shared->mutex);
        exitPid = waitpid(0, &status2, 0);
        //sem_wait(shared->mutex);
        //printf("Parent: Generator %d has exited\n", exitPid);
        //sem_post(shared->mutex);


        //clean up
        munmap(shared->actionCounter, sizeof(int));
        shm_unlink("/shmActionCounter");

        munmap(shared->hacksOnPier, sizeof(int));
        shm_unlink("/shmHacksOnPier");

        munmap(shared->serfsOnPier, sizeof(int));
        shm_unlink("/shmSerfsOnPier");

        munmap(shared->boatCounter, sizeof(int));
        shm_unlink("/shmBoatCounter");

        munmap(shared->hacksOnBoat, sizeof(int));
        shm_unlink("/shmHacksOnBoat");

        munmap(shared->serfsOnBoat, sizeof(int));
        shm_unlink("/shmSerfsOnBoat");

        munmap(shared->hacksWaitingToBoard, sizeof(int));
        shm_unlink("/shmHacksWaitingToBoard");

        munmap(shared->serfsWaitingToBoard, sizeof(int));
        shm_unlink("/shmSerfsWaitingToBoard");

        munmap(shared->membersStillToLeave, sizeof(int));
        shm_unlink("/shmMembersStillToLeave");


        sem_destroy(shared->semActionCounter);
        sem_destroy(shared->semHackCounter);
        sem_destroy(shared->semSerfCounter);
        sem_destroy(shared->semIO);

        sem_destroy(shared->semHacksOnPierQueue);
        munmap(shared->semHacksOnPierQueue, sizeof(sem_t));
        shm_unlink("/shmHacksOnPierQueue");

        sem_destroy(shared->semSerfsOnPierQueue);
        munmap(shared->semSerfsOnPierQueue, sizeof(sem_t));
        shm_unlink("/shmSerfsOnPierQueue");

        sem_destroy(shared->mutex);
        munmap(shared->mutex, sizeof(sem_t));
        shm_unlink("/shmMutex");

        sem_destroy(shared->semTurnstile1);
        munmap(shared->semTurnstile1, sizeof(sem_t));
        shm_unlink("/shmTurnstile1");

        sem_destroy(shared->semTurnstile2);
        munmap(shared->semTurnstile2, sizeof(sem_t));
        shm_unlink("/shmTurnstile2");

        sem_destroy(shared->captainsMutex);
        munmap(shared->captainsMutex, sizeof(sem_t));
        shm_unlink("/shmCaptainsMutex");

        sem_destroy(shared->semBoatRide);
        munmap(shared->semBoatRide, sizeof(sem_t));
        shm_unlink("/shmSemBoatRide");

        sem_destroy(shared->semCaptainCanLeave);
        munmap(shared->semCaptainCanLeave, sizeof(sem_t));
        shm_unlink("/shmSemCaptainCanLeave");

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
    for(int i = 0; i < args->P; i++)
    {
        int r = rand() % (spawnTime+1); //determine the spawn time for this person
        usleep(r);
        pids[i] = fork();
        if(pids[i] == 0){

            performActions(type, args, shared);

            exit(0);
        }
    }
    for(int i = 0; i < args->P; i++){
        waitpid(pids[i], NULL, 0);
    }

    //clean up
    free(pids);
    munmap(shared->hackCounter, sizeof(int));
    shm_unlink("/shmHackCounter");
    munmap(shared->serfCounter, sizeof(int));
    shm_unlink("/shmSerfCounter");
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
    int *personsWaitingToBoard;
    int *otherPersonsWaitingToBoard;
    //sem_t *semPersonsCounter;
    sem_t *semPersonsOnPierQueue;
    sem_t *semOtherPersonsOnPierQueue;
    if(type == HACK){
        strcpy(typeStr, "HACK");
        personsCounter = shared->hackCounter;
        //semPersonsCounter = shared->semHackCounter;
        personsOnPier = shared->hacksOnPier;
        semPersonsOnPierQueue = shared->semHacksOnPierQueue;
        otherPersonsOnPier = shared->serfsOnPier;
        semOtherPersonsOnPierQueue = shared->semSerfsOnPierQueue;
        personsWaitingToBoard = shared->hacksWaitingToBoard;
        otherPersonsWaitingToBoard = shared->serfsWaitingToBoard;
    }else{
        strcpy(typeStr, "SERF");
        personsCounter = shared->serfCounter;
        //semPersonsCounter = shared->semSerfCounter;
        personsOnPier = shared->serfsOnPier;
        semPersonsOnPierQueue = shared->semSerfsOnPierQueue;
        otherPersonsOnPier = shared->hacksOnPier;
        semOtherPersonsOnPierQueue = shared->semHacksOnPierQueue;
        personsWaitingToBoard = shared->serfsWaitingToBoard;
        otherPersonsWaitingToBoard = shared->hacksWaitingToBoard;
    }

    //preform action
    switch (action)
    {
        case spawn:
            sem_wait(shared->mutex);
            *id = *(personsCounter);
            fprintf(shared->fout, "%d: %s %d: starts\n", *(shared->actionCounter), typeStr, *id);
            (*(shared->actionCounter))++;
            (*(personsCounter))++;
            sem_post(shared->mutex);
            break;

        case pierAccess:
            while(true){
                if((*(shared->serfsOnPier) + *(shared->hacksOnPier)) < args->C){ //there's room
                    sem_wait(shared->mutex);
                    (*(personsOnPier))++;
                    fprintf(shared->fout, "%d: %s %d: waits: %d: %d.\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                    (*(shared->actionCounter))++;
                    sem_post(shared->mutex);
                    break;
                }else{
                    sem_wait(shared->mutex);
                    fprintf(shared->fout, "%d: %s %d: leaves queue: %d: %d.\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                    (*(shared->actionCounter))++;
                    sem_post(shared->mutex);

                    //go to sleep
                    int r = rand() % ((args->W)*1000+1)+20*1000;
                    usleep(r);

                    sem_wait(shared->mutex);
                    fprintf(shared->fout, "%d: %s %d: is back\n", *(shared->actionCounter), typeStr, *id);
                    (*(shared->actionCounter))++;
                    sem_post(shared->mutex);
                    //exit(0);
                }
            }
            break;
        
        case board:
            sem_wait(shared->mutex);
            dprintf("-- %s %d: I'll wait to be Captain\n", typeStr, *id, *(shared->boatCounter));
            sem_post(shared->mutex);

            sem_wait(shared->captainsMutex);
            sem_wait(shared->mutex);
            dprintf("-- %s %d: I'll try to be Captain\n", typeStr, *id, *(shared->boatCounter));
            sem_post(shared->mutex);
            //check if there is enough people to form crew
            if(*personsWaitingToBoard >= (4-1)){ //-1 to account for the captain
                //sem_wait(shared->captainsMutex);
                sem_wait(shared->mutex);
                for(int i = 0; i < 4; i++)
                {
                    sem_post(semPersonsOnPierQueue); //let 4 persons board
                }
                (*(personsOnPier))-=4;
                (*(personsWaitingToBoard))-=4;
                isCaptain = true; //become captain
                dprintf("-- %s %d: I'll be Captain\n", typeStr, *id, *(shared->boatCounter));
                sem_post(shared->mutex);
            }else if(*personsWaitingToBoard >= (2-1) && *otherPersonsWaitingToBoard >= 2){ //-1 to account for the captain
                //sem_wait(shared->captainsMutex);
                sem_wait(shared->mutex);
                for(int i = 0; i < 2; i++)
                {
                    sem_post(semPersonsOnPierQueue); //let 2 persons board
                }
                for(int i = 0; i < 2; i++)
                {
                    sem_post(semOtherPersonsOnPierQueue); //let 2 persons board
                }
                (*(personsOnPier))-=2;
                (*(personsWaitingToBoard))-=2;
                (*(otherPersonsOnPier))-=2;
                (*(otherPersonsWaitingToBoard))-=2;
                isCaptain = true; //become captain
                dprintf("-- %s %d: I'll be Captain\n", typeStr, *id, *(shared->boatCounter));
                sem_post(shared->mutex);
            }else{
                sem_post(shared->captainsMutex);
                sem_post(shared->mutex);
                //return 1; //didn't manage to board
            }

            sem_wait(shared->mutex);
            (*(personsWaitingToBoard))++;
            dprintf("-- %s %d: waiting in queue\n", typeStr, *id, *(shared->boatCounter));
            sem_post(shared->mutex);
            sem_wait(semPersonsOnPierQueue); //wait for the captain to let us board

            //ALL ABOARD!!!

            //barrier first phase
            sem_wait(shared->mutex);
            (*(shared->boatCounter)) += 1;
            if((*(shared->boatCounter)) == shared->boatCapacity){
                //sem_wait(shared->captainsMutex); //lock the captains mutex so no other persons try to board
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
            
            if(isCaptain){
                fprintf(shared->fout, "%d: %s %d: boards: %d: %d\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                (*(shared->actionCounter))++;
            }

            //SAIL!!!

            if(isCaptain){
                int r = rand() % ((args->R)*1000+1)+20*1000;
                usleep(r);
                sem_post(shared->semBoatRide);
            }else{
                sem_wait(shared->mutex);
                dprintf("-- %s %d: waiting for Captain to wake up\n", typeStr, *id, *(shared->boatCounter));
                sem_post(shared->mutex);
                sem_wait(shared->semBoatRide);
                sem_post(shared->semBoatRide);
            }
            dprintf("-- %s %d: Boat ride done boatCounter: %d\n", typeStr, *id, *(shared->boatCounter));

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

            //get of boat if you are not the captain (captain waits for everyone to get off)
            sem_wait(shared->semTurnstile2);
            sem_post(shared->semTurnstile2);

            if(isCaptain){
                sem_wait(shared->semCaptainCanLeave); //wait for all members to get off
                sem_wait(shared->mutex);
                fprintf(shared->fout, "%d: %s %d: captain exits: %d: %d\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                (*(shared->actionCounter))++;
                (*(shared->membersStillToLeave)) = 3; //reset the member counter
                //sem_wait(shared->semCaptainCanLeave); //relocks the semaphore
                sem_post(shared->captainsMutex);
            }else{
                sem_wait(shared->mutex);
                fprintf(shared->fout, "%d: %s %d: member exits: %d: %d\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                (*(shared->actionCounter))++;
                (*(shared->membersStillToLeave))--;
                if(*(shared->membersStillToLeave) == 0){ //are all the members gone?
                    sem_post(shared->semCaptainCanLeave);
                }
            }
            sem_post(shared->mutex);
            ret = 0;
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