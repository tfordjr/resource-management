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
void output_statistics(int, double, double, double);

volatile sig_atomic_t term = 0;  // signal handling global
struct PCB processTable[20]; // Init Process Table Array of PCB structs (not shm)
  // RESOURCE TABLE DECLARED IN RESOURCES_H

// Declaring globals needed for signal handlers to clean up at anytime
Clock* shm_clock;  // Declare global shm clock
key_t clock_key = ftok("/tmp", 35);             
int shmtid = shmget(clock_key, sizeof(Clock), IPC_CREAT | 0666);    // init shm clock
std::ofstream outputFile;   // init file object
int msgqid;           // MSGQID GLOBAL FOR MSGQ CLEANUP
int simultaneous = 1;  // simultaneous global so that sighandlers know PCB table size to avoid segfaults when killing all procs on PCB

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
        if (pid == -1){
            perror("waitpid returned -1");
            exit(1);
        } else if (pid != 0){     // if child has been terminated
            std::cout << "OSS: Receiving child has terminated..." << std::endl;
            release_all_resources(processTable, simultaneous, resourceTable, pid);
            update_process_table_of_terminated_child(processTable, pid, simultaneous);
            pid = 0;
        }

        std::cout << "OSS: attempting process unblock..." << std::endl;
        attempt_process_unblock(processTable, simultaneous, resourceTable);

        msgbuffer rcvbuf;     // NONBLOCKING WAIT TO RECEIVE MESSAGE FROM CHILD
        rcvbuf.msgCode = -1; // default msgCode used if no messages received
        if (msgrcv(msgqid, &rcvbuf, sizeof(msgbuffer), getpid(), 0) == -1) {  // IPC_NOWAIT IF 1 DOES NOT WORK
            perror("oss.cpp: Error: failed to receive message in parent\n");
            cleanup("perror encountered.");
            exit(1);
        }       // LOG MSG RECEIVE
        std::cout << "OSS: message received successfully..." << std::endl;
        if(rcvbuf.msgCode == MSG_TYPE_REQUEST){
            request_resources(processTable, simultaneous, rcvbuf.resource, rcvbuf.sender); // allocation msg to child included
        } else if (rcvbuf.msgCode == MSG_TYPE_RELEASE){
            release_single_resource(processTable, simultaneous, resourceTable, rcvbuf.sender);        
        }
        
        std::cout << "OSS: Incrementing clock, printing tables, and running dd()..." << std::endl;
        increment(shm_clock, DISPATCH_AMOUNT);  // dispatcher overhead and unblocked reschedule overhead
        print_process_table(processTable, simultaneous, shm_clock->secs, shm_clock->nanos, outputFile);
        print_resource_table(resourceTable, shm_clock->secs, shm_clock->nanos, outputFile);     
        deadlock_detection(processTable, simultaneous, resourceTable, shm_clock->secs, shm_clock->nanos);
        // CURRENTLY, WE AUTOMATICALLY GRANT RESOURCES WITHIN DD() ALGO
        // I THINK WE WANT TO REMOVE THAT AND MANUALLY DETERMINE IF RESOURCES SHOULD BE GRANTED
        // RIGHT HERE SO THAT WE CAN SEND A MESSAGE TO THE WAITING PROC AND RESUME PROC RUNTIME
    }                   // --------- END OF MAIN LOOP ---------      
    // output_statistics(totalChildren, totalTimeInSystem, totalBlockedTime, totalCPUTime);

	std::cout << "OSS: Child processes have completed. (" << numChildren << " remaining)\n";
    std::cout << "OSS: Parent is now ending.\n";
    outputFile << "OSS: Child processes have completed. (" << numChildren << " remaining)\n";
    outputFile << "OSS: Parent is now ending.\n";
    outputFile.close();  // file object close

    cleanup("OSS Success.");  // function to cleanup shm, msgq, and processes

    return 0;
}

void send_unblock_msg(msgbuffer buf){
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

void output_statistics(int totalChildren, double totalTimeInSystem, double totalBlockedTime, double totalCPUTime){
    double totalClockTime = shm_clock->secs + (shm_clock->nanos)/1e9;
    double totalWaitTime = totalTimeInSystem - (totalBlockedTime + totalCPUTime);
    std::cout << "\nRUN RESULT REPORT" << std::endl;
    std::cout << std::fixed << std::setprecision(2) << "Average Wait Time: " << totalWaitTime/totalChildren << " seconds" << std::endl;   
    std::cout << "Average CPU Utilization: " << (totalCPUTime/totalClockTime)*100 << "%" << std::endl;           
    std::cout << "Average Blocked Time: " << totalBlockedTime/totalChildren << " seconds" << std::endl; 
    std::cout << "Total Idle CPU Time: " << totalClockTime - totalCPUTime << " seconds\n" << std::endl;

    outputFile << "\nRUN RESULT REPORT" << std::endl;
    outputFile << std::fixed << std::setprecision(2) << "Average Wait Time: " << totalWaitTime/totalChildren << " seconds" << std::endl;      
    outputFile << "Average CPU Utilization: " << (totalCPUTime/totalClockTime)*100 << "%" << std::endl;          
    outputFile << "Average Blocked Time: " << totalBlockedTime/totalChildren << " seconds" << std::endl; 
    outputFile << "Total Idle CPU Time: " << totalClockTime - totalCPUTime << " seconds\n" << std::endl;
}