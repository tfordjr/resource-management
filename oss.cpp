// CS4760-001SS - Terry Ford Jr. - Project 5 Resource Management - 03/29/2024
// https://github.com/tfordjr/resource-management.git 

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <fstream>
#include "pcb.h"
#include "resources.h"
#include "clock.h"
#include "msgq.h"
#include "rng.h"
using namespace std;

void launch_child(PCB[], int);
bool launch_interval_satisfied(int);
void help();
void timeout_handler(int);
void ctrl_c_handler(int);
void cleanup(std::string);
void output_statistics();

volatile sig_atomic_t term = 0;  // signal handling global
struct PCB processTable[20]; // Init Process Table Array of PCB structs (not shm)
  // RESOURCE TABLE DECLARED IN RESOURCES_H

// Declaring globals needed for signal handlers to clean up at anytime
Clock* shm_clock;  // Declare global shm clock
key_t clock_key = ftok("/tmp", 35);             
int shmtid = shmget(clock_key, sizeof(Clock), IPC_CREAT | 0666);    // init shm clock
// std::ofstream outputFile;   // init file object
int msgqid;           // MSGQID GLOBAL FOR MSGQ CLEANUP
int simultaneous = 1;  // simultaneous global so that sighandlers know PCB table size to avoid segfaults when killing all procs on PCB
int successfulTerminations = 0;

int main(int argc, char** argv){
    int option, numChildren = 1, launch_interval = 100;      
    int totalChildren; // used for statistics metrics report
    double totalBlockedTime = 0, totalCPUTime = 0, totalTimeInSystem = 0; // used for statistics report
    string logfile = "logfile.txt";
    while ( (option = getopt(argc, argv, "hn:s:i:f:")) != -1) {   // getopt implementation
        switch(option) {
            case 'h':
                help();
                return 0;     // terminates if -h is present
            case 'n':                
                numChildren = atoi(optarg);
                totalChildren = numChildren;                
                break;
            case 's':          
                simultaneous = atoi(optarg);
                if (simultaneous > 18){
                    cout << "-s must be 18 or fewer" << endl;
                    return 0;
                }
                break;
            case 'i':
                launch_interval = (1000000 * atoi(optarg));  // converting ms to nanos
                break;            
            case 'f':
                logfile = optarg;
                break;
        }
	}   // getopt loop completed here

    std::signal(SIGALRM, timeout_handler);  // init signal handlers 
    std::signal(SIGINT, ctrl_c_handler);
    alarm(60);   // timeout timer
          
    init_process_table(processTable);      // init local process table
    init_resource_table(resourceTable);    // init resource table
    shm_clock = (Clock*)shmat(shmtid, NULL, 0);    // attatch to global clock
    shm_clock->secs = 0;                        // init clock to 00:00
    shm_clock->nanos = 0;         
    
    outputFile.open(logfile); // This will create or overwrite the file "example.txt"    
    if (!outputFile.is_open()) {
        std::cerr << "Error: logfile didn't open" << std::endl;
        return 1; // Exit with error
    }
    
         //  INITIALIZE MESSAGE QUEUE	  (MSGQID MOVED TO GLOBAL)    
	key_t msgq_key;
	system("touch msgq.txt");
	if ((msgq_key = ftok(MSGQ_FILE_PATH, MSGQ_PROJ_ID)) == -1) {   // get a key for our message queue
		perror("ftok");
		exit(1);
	}	
	if ((msgqid = msgget(msgq_key, PERMS | IPC_CREAT)) == -1) {  // create our message queue
		perror("msgget in parent");
		exit(1);
	}
	cout << "OSS: Message queue set up\n";
    outputFile << "OSS: Message queue set up\n";
  
    int i = 0;          // holds PCB location of next process
                        // For some reason my project is happier when child launches before waitpid()
                        //  ---------  MAIN LOOP  ---------   
    while(numChildren > 0 || !process_table_empty(processTable, simultaneous)){  
                // CHECK IF CONDITIONS ARE RIGHT TO LAUNCH ANOTHER CHILD
        if(numChildren > 0 && launch_interval_satisfied(launch_interval)  // check conditions to launch child
        && process_table_vacancy(processTable, simultaneous)){ // child process launch check
            std::cout << "OSS: Launching Child Process..." << endl;
            outputFile << "OSS: Launching Child Process..." << endl;
            numChildren--;
            launch_child(processTable, simultaneous);
        }

        pid_t pid = waitpid((pid_t)-1, nullptr, WNOHANG);  // non-blocking wait call for terminated child process
        if (pid > 0){     // if child has been terminated
            std::cout << "OSS: Receiving child " << pid << " has terminated! Releasing childs' resources..." << std::endl;
            
            int i = return_PCB_index_of_pid(processTable, simultaneous, pid);             
            
            if(processTable[i].occupied){  // If dd_algo() termed this pid, this ensures we don't double release resources
                release_all_resources(processTable, simultaneous, resourceTable, pid);
                update_process_table_of_terminated_child(processTable, pid, simultaneous);
            }
            pid = 0;
            successfulTerminations++;
        }  // I ignored pid == -1 case because no child procs was pid == -1 and was crashing the program
        
        attempt_process_unblock(processTable, simultaneous, resourceTable);

        msgbuffer rcvbuf;     // NONBLOCKING WAIT TO RECEIVE MESSAGE FROM CHILD
        rcvbuf.msgCode = -1; // default msgCode used if no messages received
        rcvbuf.mtype = -1;
        rcvbuf.sender = -1;           // attempt to clean rcvbuf
        rcvbuf.resource = -1;
        if (msgrcv(msgqid, &rcvbuf, sizeof(msgbuffer), getpid(), IPC_NOWAIT) == -1) {  // IPC_NOWAIT IF 1 DOES NOT WORK
            if (errno != ENOMSG){  // If the error is that no message is present, we ignore the error
                perror("oss.cpp: Error: failed to receive message in parent\n");
                cleanup("perror encountered.");
                exit(1);           
            }                                 
        }       // LOG MSG RECEIVE
        if(rcvbuf.msgCode == -1){
            std::cout << "OSS: Checked and found no messages for OSS in the msgqueue." << std::endl;
        } else if(rcvbuf.msgCode == MSG_TYPE_REQUEST){
            std::cout << "OSS: Checked and found Request msg for Resource " << static_cast<char>(65 + rcvbuf.resource) << " from pid " << rcvbuf.sender << std::endl;
            request_resources(processTable, simultaneous, rcvbuf.resource, rcvbuf.sender); // allocation msg to child included            
        } else if (rcvbuf.msgCode == MSG_TYPE_RELEASE){
            std::cout << "OSS: Checked and found Release msg from pid " << rcvbuf.sender << std::endl;
            release_single_resource(processTable, simultaneous, resourceTable, rcvbuf.sender);        
        }
       
        increment(shm_clock, DISPATCH_AMOUNT);  // dispatcher overhead and unblocked reschedule overhead
        print_process_table(processTable, simultaneous, shm_clock->secs, shm_clock->nanos, outputFile);
        print_resource_table(resourceTable, shm_clock->secs, shm_clock->nanos, outputFile);     
        deadlock_detection(processTable, simultaneous, resourceTable, shm_clock->secs, shm_clock->nanos);        
        std::cout << "Loop..." << std::endl;
    }                   // --------- END OF MAIN LOOP ---------    

    output_statistics();

	std::cout << "OSS: Child processes have completed. (" << numChildren << " remaining)\n";
    std::cout << "OSS: Parent is now ending.\n";
    outputFile << "OSS: Child processes have completed. (" << numChildren << " remaining)\n";
    outputFile << "OSS: Parent is now ending.\n";
    outputFile.close();  // file object close

    cleanup("OSS Success.");  // function to cleanup shm, msgq, and processes

    return 0;
}

void send_msg_to_child(msgbuffer buf){
    if (msgsnd(msgqid, &buf, sizeof(msgbuffer), 0) == -1) { 
        perror("msgsnd to parent failed\n");
        exit(1);
    }
}           

void launch_child(PCB processTable[], int simultaneous){
    pid_t childPid = fork(); // This is where the child process splits from the parent        
    if (childPid == 0) {            // Each child uses exec to run ./user	
        execl("./user", "./user", nullptr);
        perror("oss.cpp: launch_child(): execl() has failed!");
        exit(EXIT_FAILURE);
    } else if (childPid == -1) {  // Fork failed
        perror("oss.cpp: Error: Fork has failed");
        exit(EXIT_FAILURE);
    } else {            // Parent updates Process Table with child info after fork()
        int i = (process_table_vacancy(processTable, simultaneous) - 1);
        processTable[i].occupied = 1;
        processTable[i].pid = childPid;
        processTable[i].startSecs = shm_clock->secs;
        processTable[i].startNanos = shm_clock->nanos;
        processTable[i].blocked = 0;
        for(int j = 0; j < NUM_RESOURCES; j++){
            processTable[i].resourcesHeld[j] = 0;
        }
        increment(shm_clock, CHILD_LAUNCH_AMOUNT);  // child launch overhead simulated
    }
}

bool launch_interval_satisfied(int launch_interval){
    static int last_launch_secs = 0;  // static ints used to keep track of 
    static int last_launch_nanos = 0;   // most recent process launch

    int elapsed_secs = shm_clock->secs - last_launch_secs; 
    int elapsed_nanos = shm_clock->nanos - last_launch_nanos;

    while (elapsed_nanos < 0) {   // fix if subtracted time is too low
        elapsed_secs--;
        elapsed_nanos += 1000000000;
    }

    if (elapsed_secs > 0 || (elapsed_secs == 0 && elapsed_nanos >= launch_interval)) {        
        last_launch_secs = shm_clock->secs;  // Update the last launch time
        last_launch_nanos = shm_clock->nanos;        
        return true;
    } else {
        return false;
    }
}

void help(){   // Help message here
    printf("-h detected. Printing Help Message...\n");
    printf("The options for this program are: \n");
    printf("\t-h Help will halt execution, print help message, and take no arguments.\n");
    printf("\t-n The argument following -n will be number of total processes to be run.\n");
    printf("\t-s The argument following -s will be max number of processes to be run simultaneously\n");
    printf("\t-t The argument following -t will be the max time limit for each user process created.\n");
    printf("\t-i The argument following -i will be launch interval between process launch in milliseconds.\n");
    printf("\t-i The argument following -f will be the logfile name (please include file extention)\n");
    printf("\t args will default to appropriate values if not provided.\n");
}

void timeout_handler(int signum) {
    cleanup("Timeout Occurred.");
}

// Signal handler for Ctrl+C (SIGINT)
void ctrl_c_handler(int signum) {    
    cleanup("Ctrl+C detected.");
}

void cleanup(std::string cause) {
    std::cout << cause << " Cleaning up before exiting..." << std::endl;
    outputFile << cause << " Cleaning up before exiting..." << std::endl;
    kill_all_processes(processTable, simultaneous);
    outputFile.close();  // file object close
    shmdt(shm_clock);       // clock cleanup, detatch & delete shm
    if (shmctl(shmtid, IPC_RMID, NULL) == -1) {
        perror("oss.cpp: Error: shmctl failed!!");
        exit(1);
    }            
    if (msgctl(msgqid, IPC_RMID, NULL) == -1) {  // get rid of message queue
		perror("oss.cpp: Error: msgctl to get rid of queue in parent failed");
		exit(1);
	}
    std::exit(EXIT_SUCCESS);
}

void output_statistics(){           
    std::cout << "\nRUN RESULT REPORT" << std::endl;
    std::cout << "Requests granted immediately: " << requestsImmediatelyGranted << std::endl;   
    std::cout << "Requests granted from blocked queue: " << requestsEventuallyGranted << std::endl; 
    std::cout << "Times deadlock detection algorithm has run: " << ddAlgoRuns << std::endl;
    std::cout << "Processes terminated by deadlock detection algorithm: " << ddAlgoKills << std::endl; 
    std::cout << "Processes terminated successfully without intervention: " << (successfulTerminations - ddAlgoKills) << std::endl;
    std::cout << "Average number of terminations required to resolve a deadlock: " << std::fixed << std::setprecision(1) << static_cast<double>(ddAlgoKills)/numDeadlocks << std::endl;    

    outputFile << "\nRUN RESULT REPORT" << std::endl;
    outputFile << "Requests granted immediately: " << requestsImmediatelyGranted << std::endl;   
    outputFile << "Requests granted from blocked queue: " << requestsEventuallyGranted << std::endl; 
    outputFile << "Times deadlock detection algorithm has run: " << ddAlgoRuns << std::endl;
    outputFile << "Processes terminated by deadlock detection algorithm: " << ddAlgoKills << std::endl; 
    outputFile << "Processes terminated successfully without intervention: " << (successfulTerminations - ddAlgoKills) << std::endl;
    outputFile << "Average number of terminations required to resolve a deadlock: " << std::fixed << std::setprecision(1) << static_cast<double>(ddAlgoKills)/numDeadlocks << std::endl;   
}