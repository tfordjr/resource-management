// CS4760-001SS - Terry Ford Jr. - Project 4 OSS Scheduler - 03/08/2024
// https://github.com/tfordjr/oss-scheduler.git

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <cstring>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <errno.h>
#include "clock.h"
#include "msgq.h"
using namespace std;

#define TERMINATION_CHANCE 10
#define IO_BLOCK_CHANCE 25
#define ACUTAL_IO_BLOCK_CHANCE (TERMINATION_CHANCE + IO_BLOCK_CHANCE)

void calculate_time_until_unblocked(int, int, int, int *, int *);
bool will_process_terminate_during_quantum(int, int, int, int, int, int, int, int*);

int main(int argc, char** argv) {
    Clock* shm_clock;           // init shm clock
	key_t clock_key = ftok("/tmp", 35);
	int shmtid = shmget(clock_key, sizeof(Clock), 0666);
	shm_clock = (Clock*)shmat(shmtid, NULL, 0);

    int start_secs = shm_clock->secs;  // start time is current time at the start
    int start_nanos = shm_clock->nanos; // Is this our issue leaving child in sys too long?
  
    int secs = atoi(argv[1]);     // Arg 1 will be secs, secs given from start to terminate
    int nanos = atoi(argv[2]);   // Arg 2 will be nanos  
    
    int end_secs = start_secs + secs;   // end time is starting time plus time told to wait
    int end_nanos = start_nanos + nanos;  
    if (end_nanos >= 1000000000){   // if over 1 billion nanos, add 1 second, sub 1 bil nanos
        end_nanos = end_nanos - 1000000000;
        end_secs++;
    } 

    msgbuffer buf, rcvbuf;   // buf for msgsnd buffer, rcvbuf for msgrcv buffer
	buf.mtype = getpid();
	int msgqid = 0;
	key_t msgq_key;	
	if ((msgq_key = ftok(MSGQ_FILE_PATH, MSGQ_PROJ_ID)) == -1) {   // get a key for our message queue
		perror("ftok");
		exit(1);
	}	
	if ((msgqid = msgget(msgq_key, PERMS)) == -1) {  // create our message queue
		perror("msgget in child");
		exit(1);
	}
	printf("%d: Child has access to the msg queue\n",getpid());   // starting messages    
    printf("USER PID: %d  PPID: %d  SysClockS: %d  SysClockNano: %d  TermTimeS: %d  TermTimeNano: %d\n--Just Starting\n", getpid(), getppid(), shm_clock->secs, shm_clock->nanos, end_secs, end_nanos); 

    int iter = 0;
    bool done = false;
    while(!done){      // ----------- MAIN LOOP -----------     
        iter++;       // MSGRCV BLOCKING WAIT, WAITS HERE WHILE IO BLOCKED ALSO
        if ( msgrcv(msgqid, &rcvbuf, sizeof(msgbuffer), getpid(), 0) == -1) {
            perror("failed to receive message from parent\n");
            exit(1);
        }       // MSGRCV PRINT MSG	
        printf("%d: Child received message code: %d from parent\n",getpid(), rcvbuf.msgCode);

                // INIT RANDOM CHANCE VARS       
        srand(getpid() + time(NULL)); // Should be different for every run of every proc.
        int random_number = rand() % 100;  
        srand(getpid() + time(NULL));  // reseeding rand()

                    // IF WE RANDOMLY TERM EARLY
        if (random_number < TERMINATION_CHANCE){   
            printf("USER PID: %d  PPID: %d  SysClockS: %d  SysClockNano: %d  TermTimeS: %d  TermTimeNano: %d\n--Terminating after sending message back to oss after %d iterations.\n", getpid(), getppid(), shm_clock->secs, shm_clock->nanos, end_secs, end_nanos, iter);
            done = true;
            buf.msgCode = MSG_TYPE_SUCCESS;    
            strcpy(buf.message,"Completed Successfully (RANDOM TERMINATION), now terminating...\n");
            buf.time_slice = rand() % rcvbuf.time_slice;  // use random amount of timeslice before terminating            
                    // IF WE RANDOMLY IO BLOCK
        } else if (random_number < ACUTAL_IO_BLOCK_CHANCE){   
            buf.time_slice = rand() % rcvbuf.time_slice;  // use random amount of timeslice before IO Block
            buf.msgCode = MSG_TYPE_BLOCKED;            
            int nanos_blocked = 1000000000; // IO BLOCK ALWAYS LASTS 1 SEC
            int temp_unblock_secs, temp_unblock_nanos;
            calculate_time_until_unblocked(shm_clock->secs, shm_clock->nanos, nanos_blocked, &temp_unblock_secs, &temp_unblock_nanos);
            buf.blocked_until_secs = temp_unblock_secs;
            buf.blocked_until_nanos = temp_unblock_nanos;
                    // IF WE NEITHER TERM EARLY OR IO BLOCK
        } else { // check if end time has already elapsed, if so, terminate asap
            int secs_plus_quantum, nanos_plus_quantum, time_slice_used;
            if(shm_clock->secs > end_secs || shm_clock->secs == end_secs && shm_clock->nanos > end_nanos){ 
                printf("USER PID: %d  PPID: %d  SysClockS: %d  SysClockNano: %d  TermTimeS: %d  TermTimeNano: %d\n--Terminating after sending message back to oss after %d iterations.\n", getpid(), getppid(), shm_clock->secs, shm_clock->nanos, end_secs, end_nanos, iter);
                done = true;
                buf.msgCode = MSG_TYPE_SUCCESS; 
                strcpy(buf.message,"Completed Successfully (END TIME ELAPSED), now terminating...\n");
                    // HOW MUCH OF THE TIME SLICE DID YOU USE ?
            } else if(will_process_terminate_during_quantum(shm_clock->secs, shm_clock->nanos, end_secs, end_nanos, rcvbuf.time_slice, secs_plus_quantum, nanos_plus_quantum, &time_slice_used)){

            } else {    // else program continues running
                // IF TERM WOULD ELAPSE DURING RUNTIME, CALC USED TIME SLICE, EXECUTE,
                // ELSE, TERM TIME WOULD NOT ELASE DURING RUNTIME, TIME SLICE IS FULL QUANTUM
                
                printf("USER PID: %d  PPID: %d  SysClockS: %d  SysClockNano: %d  TermTimeS: %d  TermTimeNano: %d\n--%d iteration(s) have passed since starting\n", getpid(), getppid(), shm_clock->secs, shm_clock->nanos, end_secs, end_nanos, iter);
                buf.msgCode = MSG_TYPE_RUNNING;
                buf.time_slice = rcvbuf.time_slice;  // used full time slice
                strcpy(buf.message,"Still Running...\n");
                // Check again that process doesn't term naturally during runtime
            }
        }
        
            // msgsnd(to parent saying if we are done or not);
        if (msgsnd(msgqid, &buf, sizeof(msgbuffer), 0) == -1) {
            perror("msgsnd to parent failed\n");
            exit(1);
        }
    }   // ----------- MAIN LOOP -----------     

    shmdt(shm_clock);  // deallocate shm and terminate
    printf("%d: Child is terminating...\n",getpid());
    return EXIT_SUCCESS; 
}

void calculate_time_until_unblocked(int secs, int nanos, int nanos_blocked, int *temp_unblock_secs, int *temp_unblock_nanos){
    nanos += nanos_blocked;
    if (nanos >= 1000000000){   // if over 1 billion nanos, add 1 second, sub 1 bil nanos
        nanos -= 1000000000;
        secs++;
    }
    *temp_unblock_secs = secs;
    *temp_unblock_nanos = nanos;
    return;
}

bool will_process_terminate_during_quantum(int secs, int nanos, int end_secs, int end_nanos, int quantum, int secs_plus_quantum, int nanos_plus_quantum, int* time_slice_used){
    secs_plus_quantum = secs;
    nanos_plus_quantum = nanos;
    
    nanos_plus_quantum += quantum;

    if (nanos >= 1000000000){   // if over 1 billion nanos, add 1 second, sub 1 bil nanos
        nanos_plus_quantum -= 1000000000;
        secs_plus_quantum++;
    }
    
    int elapsed_secs = end_secs - secs_plus_quantum;  // difference between termination time and clock time
    int elapsed_nanos = end_nanos - nanos_plus_quantum;    
   
    if (elapsed_nanos < 0) {  
        elapsed_secs--;       
        elapsed_nanos += 1000000000;
    }
        // if difference is negative, process will end during quantum
    if(elapsed_secs < 0){
        *time_slice_used = abs(elapsed_secs * 1000000000 + elapsed_nanos);
        return true;
    }
         // Try using this if the above is too dangerous, with how short our quantums are,
         // this will suffice for all of the cases we will see.
    // if(elapsed_secs == -1){   
    //     *time_slice_used = abs(1000000000 + elapsed_nanos);
    //     return true;
    // }

        // else program will not end during next quantum
    return false;
}