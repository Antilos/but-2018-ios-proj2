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

        case 4:
            errMsg("File not opened correctly");
            break;
    
        default:
            errMsg("!!!SOMENE FORGOT TO WRITE AN ERROR MESSAGE!!!");
    }

    return ErrorNum;
}

int mainWrapper(int argc, char* argv[]){
    int errNum = 0;

    /*argument handling*/
    args_t *args = malloc(sizeof(args_t));
    int argCode = parseArgs(argc, argv, args);
    if(argCode != 0){
        errNum = argCode;
        //goto cleanup;
    }

    /*shared holder*/
    shm_sem_t *shared = malloc(sizeof(shm_sem_t));
    shared->boatCapacity = 4; //initialize boat capacity

    /*shared mutex*/ //needs to be shared, as second generation children need to see it
    int shmMutex = shm_open("/shm_xkocal00_Mutex", O_CREAT | O_RDWR, 0666);
    ftruncate(shmMutex, sizeof(sem_t));
    shared->mutex = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmMutex, 0);
    close(shmMutex);
    if(sem_init(shared->mutex, 1, 1) < 0){
        errNum = 3; //Error while initializing semaphore
        //goto cleanup;
    }

    /*IO semaphore*/
    shared->semIO = (sem_t*)malloc(sizeof(sem_t));
    if(sem_init(shared->semIO, 1, 1) < 0){
        errNum = 3; //Error while initializing semaphore
        //goto cleanup;
    }

    /*shared action counter*/
    int shmActionCounter = shm_open("/shm_xkocal00_ActionCounter", O_CREAT | O_RDWR, 0666);
    ftruncate(shmActionCounter, sizeof(int));
    shared->actionCounter = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmActionCounter, 0);
    close(shmActionCounter);
    *(shared->actionCounter) = 1; //initialization
    // shared->semActionCounter = (sem_t*)malloc(sizeof(sem_t)); //semaphore guarding the action counter
    // if(sem_init(shared->semActionCounter, 1, 1) < 0){
    //     errNum = 3; //Error while initializing semaphore
    //     //goto cleanup;
    // }

    /*shared hacks on pier counter*/
    int shmHacksOnPier = shm_open("/shm_xkocal00_HacksOnPier", O_CREAT | O_RDWR, 0666);
    ftruncate(shmHacksOnPier, sizeof(int));
    shared->hacksOnPier = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmHacksOnPier, 0);
    close(shmHacksOnPier);
    *(shared->hacksOnPier) = 0; //initialization
    
    /*shared serfs on pier counter*/
    int shmSerfsOnPier = shm_open("/shm_xkocal00_SerfsOnPier", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSerfsOnPier, sizeof(int));
    shared->serfsOnPier = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmSerfsOnPier, 0);
    close(shmSerfsOnPier);
    *(shared->serfsOnPier) = 0; //initialization

     /*shared boat counter*/
    int shmBoatCounter = shm_open("/shm_xkocal00_BoatCounter", O_CREAT | O_RDWR, 0666);
    ftruncate(shmBoatCounter, sizeof(int));
    shared->boatCounter = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmBoatCounter, 0);
    close(shmBoatCounter);
    *(shared->boatCounter) = 0; //initialization

    /*shared hacks in queue counter*/
    int shmHacksWaitingToBoard = shm_open("/shm_xkocal00_HacksWaitingToBoard", O_CREAT | O_RDWR, 0666);
    ftruncate(shmHacksWaitingToBoard, sizeof(int));
    shared->hacksWaitingToBoard = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmHacksWaitingToBoard, 0);
    close(shmHacksWaitingToBoard);
    *(shared->hacksWaitingToBoard) = 0; //initialization

    /*shared serfs in queue counter*/
    int shmSerfsWaitingToBoard = shm_open("/shm_xkocal00_SerfsWaitingToBoard", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSerfsWaitingToBoard, sizeof(int));
    shared->serfsWaitingToBoard = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmSerfsWaitingToBoard, 0);
    close(shmSerfsWaitingToBoard);
    *(shared->serfsWaitingToBoard) = 0; //initialization

    /*HacksOnPierQueue*/
    int shmHacksOnPierQueue = shm_open("/shm_xkocal00_HacksOnPierQueue", O_CREAT | O_RDWR, 0666);
    ftruncate(shmHacksOnPierQueue, sizeof(sem_t));
    shared->semHacksOnPierQueue = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmHacksOnPierQueue, 0);
    close(shmHacksOnPierQueue);
    if(sem_init(shared->semHacksOnPierQueue, 1, 0) < 0){
        errNum = 3; //Error while initializing semaphore
        //goto cleanup;
    }

    /*SerfsOnPierQueue*/
    int shmSerfsOnPierQueue = shm_open("/shm_xkocal00_SerfsOnPierQueue", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSerfsOnPierQueue, sizeof(sem_t));
    shared->semSerfsOnPierQueue = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmSerfsOnPierQueue, 0);
    close(shmSerfsOnPierQueue);
    if(sem_init(shared->semSerfsOnPierQueue, 1, 0) < 0){
        errNum = 3; //Error while initializing semaphore
        //goto cleanup;
    }

    /*shared hacks on boat counter*/
    int shmHacksOnBoat = shm_open("/shm_xkocal00_HacksOnBoat", O_CREAT | O_RDWR, 0666);
    ftruncate(shmHacksOnBoat, sizeof(int));
    shared->hacksOnBoat = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmHacksOnBoat, 0);
    close(shmHacksOnBoat);
    *(shared->hacksOnBoat) = 0; //initialization

    /*shared serfs on boat counter*/
    int shmSerfsOnBoat = shm_open("/shm_xkocal00_SerfsOnBoat", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSerfsOnBoat, sizeof(int));
    shared->serfsOnBoat = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmSerfsOnBoat, 0);
    close(shmSerfsOnBoat);
    *(shared->serfsOnBoat) = 0; //initialization

    //turnstile semaphores for barrier implementation (double randezvouse)
    int shmTurnstile1 = shm_open("/shm_xkocal00_Turnstile1", O_CREAT | O_RDWR, 0666);
    ftruncate(shmTurnstile1, sizeof(sem_t));
    shared->semTurnstile1 = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmTurnstile1, 0); //bars entry to crit section untill all processes have arrived
    close(shmTurnstile1);
    if(sem_init(shared->semTurnstile1, 1, 0) < 0){ //locked
        errNum = 3; //Error while initializing semaphore
        //goto cleanup;
    }

    int shmTurnstile2 = shm_open("/shm_xkocal00_Turnstile2", O_CREAT | O_RDWR, 0666);
    ftruncate(shmTurnstile2, sizeof(sem_t));
    shared->semTurnstile2 = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmTurnstile2, 0);//makes processes wait until all other processes have finished crit section
    close(shmTurnstile2);
    if(sem_init(shared->semTurnstile2, 1, 1) < 0){ //unlocked
        errNum = 3; //Error while initializing semaphore
        //goto cleanup;
    }

     /*mutex for when a process is checking if whether it can become captain*/
    int shmCaptainsMutex = shm_open("/shm_xkocal00_CaptainsMutex", O_CREAT | O_RDWR, 0666);
    ftruncate(shmCaptainsMutex, sizeof(sem_t));
    shared->captainsMutex = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmCaptainsMutex, 0);
    close(shmCaptainsMutex);
    if(sem_init(shared->captainsMutex, 1, 1) < 0){
        errNum = 3; //Error while initializing semaphore
        //goto cleanup;
    }

    /*Semaphore to keep persons from exiting the boat before the ride is over*/
    int shmSemBoatRide = shm_open("/shm_xkocal00_SemBoatRide", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSemBoatRide, sizeof(sem_t));
    shared->semBoatRide = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmSemBoatRide, 0);
    close(shmSemBoatRide);
    if(sem_init(shared->semBoatRide, 1, 0) < 0){
        errNum = 3; //Error while initializing semaphore
        //goto cleanup;
    }

    /*Semaphore to keep the captain from leaving before all members are gone*/
    int shmSemCaptainCanLeave = shm_open("/shm_xkocal00_SemCaptainCanLeave", O_CREAT | O_RDWR, 0666);
    ftruncate(shmSemCaptainCanLeave, sizeof(sem_t));
    shared->semCaptainCanLeave = (sem_t*)mmap(0, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED, shmSemCaptainCanLeave, 0);
    close(shmSemCaptainCanLeave);
    if(sem_init(shared->semCaptainCanLeave, 1, 0) < 0){
        errNum = 3; //Error while initializing semaphore
        //goto cleanup;
    }

    /*shared serfs on boat counter*/
    int shmMembersStillToLeave = shm_open("/shm_xkocal00_MembersStillToLeave", O_CREAT | O_RDWR, 0666);
    ftruncate(shmMembersStillToLeave, sizeof(int));
    shared->membersStillToLeave = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmMembersStillToLeave, 0);
    close(shmMembersStillToLeave);
    *(shared->membersStillToLeave) = 3; //initialization (3, since boat capacity is 4)

    /*open output file*/
    //FILE* outputFile = fopen("proj2.out", "w");
    shared->fout = fopen("proj2.out", "w");
    if(shared->fout == NULL){
        errNum = 4; //File open error
        //goto cleanup;
    }
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
        fclose(shared->fout);

        munmap(shared->actionCounter, sizeof(int));
        shm_unlink("/shm_xkocal00_ActionCounter");

        munmap(shared->hacksOnPier, sizeof(int));
        shm_unlink("/shm_xkocal00_HacksOnPier");

        munmap(shared->serfsOnPier, sizeof(int));
        shm_unlink("/shm_xkocal00_SerfsOnPier");

        munmap(shared->boatCounter, sizeof(int));
        shm_unlink("/shm_xkocal00_BoatCounter");

        munmap(shared->hacksOnBoat, sizeof(int));
        shm_unlink("/shm_xkocal00_HacksOnBoat");

        munmap(shared->serfsOnBoat, sizeof(int));
        shm_unlink("/shm_xkocal00_SerfsOnBoat");

        munmap(shared->hacksWaitingToBoard, sizeof(int));
        shm_unlink("/shm_xkocal00_HacksWaitingToBoard");

        munmap(shared->serfsWaitingToBoard, sizeof(int));
        shm_unlink("/shm_xkocal00_SerfsWaitingToBoard");

        munmap(shared->membersStillToLeave, sizeof(int));
        shm_unlink("/shm_xkocal00_MembersStillToLeave");


        sem_destroy(shared->semActionCounter);
        sem_destroy(shared->semHackCounter);
        sem_destroy(shared->semSerfCounter);
        sem_destroy(shared->semIO);

        sem_destroy(shared->semHacksOnPierQueue);
        munmap(shared->semHacksOnPierQueue, sizeof(sem_t));
        shm_unlink("/shm_xkocal00_HacksOnPierQueue");

        sem_destroy(shared->semSerfsOnPierQueue);
        munmap(shared->semSerfsOnPierQueue, sizeof(sem_t));
        shm_unlink("/shm_xkocal00_SerfsOnPierQueue");

        sem_destroy(shared->mutex);
        munmap(shared->mutex, sizeof(sem_t));
        shm_unlink("/shm_xkocal00_Mutex");

        sem_destroy(shared->semTurnstile1);
        munmap(shared->semTurnstile1, sizeof(sem_t));
        shm_unlink("/shm_xkocal00_Turnstile1");

        sem_destroy(shared->semTurnstile2);
        munmap(shared->semTurnstile2, sizeof(sem_t));
        shm_unlink("/shm_xkocal00_Turnstile2");

        sem_destroy(shared->captainsMutex);
        munmap(shared->captainsMutex, sizeof(sem_t));
        shm_unlink("/shm_xkocal00_CaptainsMutex");

        sem_destroy(shared->semBoatRide);
        munmap(shared->semBoatRide, sizeof(sem_t));
        shm_unlink("/shm_xkocal00_SemBoatRide");

        sem_destroy(shared->semCaptainCanLeave);
        munmap(shared->semCaptainCanLeave, sizeof(sem_t));
        shm_unlink("/shm_xkocal00_SemCaptainCanLeave");

        return errNum;
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
        int shmHackCounter = shm_open("/shm_xkocal00_HackCounter", O_CREAT | O_RDWR, 0666);
        ftruncate(shmHackCounter, sizeof(int));
        shared->hackCounter = (int*)mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shmHackCounter, 0);
        *(shared->hackCounter) = 0; //persons counter initialization

        shared->semHackCounter = (sem_t*)malloc(sizeof(sem_t)); //semaphore guarding the persons counter
        if(sem_init(shared->semHackCounter, 1, 1) < 0){
            return 3; //Error while initializing semaphore
        }
    }else{
        int shmSerfCounter = shm_open("/shm_xkocal00_SerfCounter", O_CREAT | O_RDWR, 0666);
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
    shm_unlink("/shm_xkocal00_HackCounter");
    munmap(shared->serfCounter, sizeof(int));
    shm_unlink("/shm_xkocal00_SerfCounter");
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
            fprintf(shared->fout, "%d:\t%s\t%d:\tstarts\n", *(shared->actionCounter), typeStr, *id);
            fflush(shared->fout);
            (*(shared->actionCounter))++;
            (*(personsCounter))++;
            sem_post(shared->mutex);
            break;

        case pierAccess:
            while(true){
                if((*(shared->serfsOnPier) + *(shared->hacksOnPier)) < args->C){ //there's room
                    sem_wait(shared->mutex);
                    (*(personsOnPier))++;
                    fprintf(shared->fout, "%d:\t%s\t%d:\twaits:\t%d:\t%d.\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                    fflush(shared->fout);
                    (*(shared->actionCounter))++;
                    sem_post(shared->mutex);
                    break;
                }else{
                    sem_wait(shared->mutex);
                    fprintf(shared->fout, "%d:\t%s\t%d:\tleaves queue:\t%d:\t%d.\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                    fflush(shared->fout);
                    (*(shared->actionCounter))++;
                    sem_post(shared->mutex);

                    //go to sleep
                    int r = rand() % ((args->W)*1000+1)+20*1000;
                    usleep(r);

                    sem_wait(shared->mutex);
                    fprintf(shared->fout, "%d:\t%s\t%d:\tis back\n", *(shared->actionCounter), typeStr, *id);
                    fflush(shared->fout);
                    (*(shared->actionCounter))++;
                    sem_post(shared->mutex);
                    //exit(0);
                }
            }
            break;
        
        case board:
            //debug print was here
            
            sem_wait(shared->mutex);
            fprintf(stderr, "DEBUGBefore\n");
            sem_post(shared->mutex);
            sem_wait(shared->captainsMutex);
            //debug print was here
            //check if there is enough people to form crew
            sem_wait(shared->mutex);
            if(*personsWaitingToBoard >= (4-1)){ //-1 to account for the captain
                for(int i = 0; i < 4; i++)
                {
                    sem_post(semPersonsOnPierQueue); //let 4 persons board
                }
                (*(personsOnPier))-=4;
                (*(personsWaitingToBoard))-=4;
                //debug print was here
            }else if(*personsWaitingToBoard >= (2-1) && *otherPersonsWaitingToBoard >= 2){ //-1 to account for the captain
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
                //debug print was here
            }else{
                sem_post(shared->captainsMutex);
                //return 1; //didn't manage to board
            }
            sem_post(shared->mutex);

            sem_wait(shared->mutex);
            fprintf(stderr, "DEBUGAfter\n");
            sem_post(shared->mutex);

            sem_wait(shared->mutex);
            (*(personsWaitingToBoard))++;
            //debug print was here
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
            
            //debug print was here

            //wait for the boat to be fully boarded
            sem_wait(shared->semTurnstile1);
            sem_post(shared->semTurnstile1);
            //debug print was here
            
            if(isCaptain){
                sem_wait(shared->mutex);
                fprintf(shared->fout, "%d:\t%s\t%d:\tboards:\t%d:\t%d\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                fflush(shared->fout);
                (*(shared->actionCounter))++;
                sem_wait(shared->mutex);
            }

            //SAIL!!!

            if(isCaptain){
                int r = rand() % ((args->R)*1000+1)+20*1000;
                usleep(r);
                sem_post(shared->semBoatRide);
            }else{
                //debug print was here
                sem_wait(shared->semBoatRide);
                sem_post(shared->semBoatRide);
            //debug print was here
            }

            //barrier second phase
            sem_wait(shared->mutex);
            (*(shared->boatCounter)) -= 1;
            if((*(shared->boatCounter)) == 0){
                sem_wait(shared->semTurnstile1);
                sem_post(shared->semTurnstile2);
            }
            sem_post(shared->mutex);

            //debug print was here

            //get of boat if you are not the captain (captain waits for everyone to get off)
            sem_wait(shared->semTurnstile2);
            sem_post(shared->semTurnstile2);

            if(isCaptain){
                sem_wait(shared->semCaptainCanLeave); //wait for all members to get off
                sem_wait(shared->mutex);
                fprintf(shared->fout, "%d:\t%s\t%d:\tcaptain exits:\t%d:\t%d\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                fflush(shared->fout);
                (*(shared->actionCounter))++;
                (*(shared->membersStillToLeave)) = 3; //reset the member counter
                //sem_wait(shared->semCaptainCanLeave); //relocks the semaphore
                sem_post(shared->captainsMutex);
            }else{
                sem_wait(shared->mutex);
                fprintf(shared->fout, "%d:\t%s\t%d:\tmember exits:\t%d:\t%d\n", *(shared->actionCounter), typeStr, *id, *(shared->hacksOnPier), *(shared->serfsOnPier));
                fflush(shared->fout);
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