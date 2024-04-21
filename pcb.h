// CS4760-001SS - Terry Ford Jr. - Project 6 Memory Management - 04/21/2024
// https://github.com/tfordjr/memory-management.git

#ifndef PCB_H
#define PCB_H

#define NUM_RESOURCES 10
#define NUM_INSTANCES 20

#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <fstream>
#include <string>

struct PCB {
    int occupied;                 // bool occupied or not
    pid_t pid;                    // pid 
    int startSecs;                // time when it was forked
    int startNanos;               
    int blocked;                  // bool blocked or not
    int resourcesHeld[NUM_RESOURCES];  
};

void init_process_table(PCB processTable[]){
    for(int i = 0; i < 20; i++){
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].startSecs = 0;
        processTable[i].startNanos = 0;
        processTable[i].blocked = 0;
        for(int j = 0; j < NUM_RESOURCES; j++){
            processTable[i].resourcesHeld[j] = 0;
        }        
    }
}

int process_table_vacancy(PCB processTable[], int simultaneous){
    for(int i = 0; i < simultaneous; i++){
        if (!processTable[i].occupied){
            return (i + 1);
        }
    }
    return 0;
}

int running_processes(PCB processTable[], int simultaneous){
    int numProcesses = 0;
    for(int i = 0; i < simultaneous; i++){
        if (processTable[i].occupied){
            numProcesses++;
        }
    }      // returns no lower than 1 to prevent divide by 0 error in clock.h::increment()
    return (numProcesses == 0) ? 1 : numProcesses;  
}

bool process_table_empty(PCB processTable[], int simultaneous){
    for(int i = 0; i < simultaneous; i++){
        if (processTable[i].occupied){
            return 0;
        }
    }
    return 1;
}

bool all_processes_blocked(PCB processTable[], int simultaneous){
    for(int i = 0; i < simultaneous; i++){
        if (processTable[i].occupied && !processTable[i].blocked){
            return 0;
        }
    }
    return 1;
}

void print_process_table(PCB processTable[], int simultaneous, int secs, int nanos, std::ostream& outputFile){
    static int next_print_secs = 0;  // static ints used to keep track of each 
    static int next_print_nanos = 0;   // process table print to be done

    if(secs > next_print_secs || (secs == next_print_secs && nanos > next_print_nanos)){
        printf("OSS PID: %d  SysClockS: %d  SysClockNano: %d  \nProcess Table:\nEntry\tOccupied  PID\tStartS\tStartN\t\tBlocked\tUnblockedS  UnblockedN\n", getpid(), secs, nanos);
        outputFile << "OSS PID: " << getpid() << "  SysClockS: " << secs << "  SysClockNano " << nanos << "  \nProcess Table:\nEntry\tOccupied  PID\tStartS\tStartN\t\tBlocked\tUnblockedS  UnblockedN\n";
        for(int i = 0; i < simultaneous; i++){
            std::string tab = (processTable[i].startNanos == 0) ? "\t\t" : "\t";
            std::string r_list = "";

            for(int j = 0; j < NUM_RESOURCES; j++){
                r_list += static_cast<char>(65 + j);
                r_list += ":";
                r_list += std::to_string(processTable[i].resourcesHeld[j]);
                r_list += " ";
            }

            std::cout << std::to_string(i + 1) << "\t" << std::to_string(processTable[i].occupied) << "\t" << std::to_string(processTable[i].pid) << "\t" << std::to_string(processTable[i].startSecs) << "\t" << std::to_string(processTable[i].startNanos) << tab << std::to_string(processTable[i].blocked) << "\t" << r_list << std::endl;
            outputFile << std::to_string(i + 1) << "\t" << std::to_string(processTable[i].occupied) << "\t" << std::to_string(processTable[i].pid) << "\t" << std::to_string(processTable[i].startSecs) << "\t" << std::to_string(processTable[i].startNanos) << tab << std::to_string(processTable[i].blocked) << "\t" << r_list << std::endl;
        }
        next_print_nanos = next_print_nanos + 500000000;
        if (next_print_nanos >= 1000000000){   // if over 1 billion nanos, add 1 second, sub 1 bil nanos
            next_print_nanos = next_print_nanos - 1000000000;
            next_print_secs++;
        }    
    }
}

void update_process_table_of_terminated_child(PCB processTable[], pid_t pid, int simultaneous){
    for(int i = 0; i < simultaneous; i++){
        if(processTable[i].pid == pid){  // if PCB pid equal to killed pid
            processTable[i].occupied = 0;
            processTable[i].pid = 0;
            processTable[i].startSecs = 0;
            processTable[i].startNanos = 0;
            processTable[i].blocked = 0;
            for(int j = 0; j < NUM_RESOURCES; j++){
                processTable[i].resourcesHeld[j] = 0;
            }  
            return;
        } 
    }
}
       
void update_process_table_of_blocked_child(PCB processTable[], pid_t pid, int simultaneous, int blockedUntilSec, int blockedUntilNano){
    for(int i = 0; i < simultaneous; i++){
        if(processTable[i].pid == pid){  // if PCB pid equal to blocked pid            
            processTable[i].blocked = 1;
            return;
        } 
    }
}
    
void kill_all_processes(PCB processTable[], int simultaneous){
    for(int i = 0; i < simultaneous; i++){
        if(processTable[i].occupied){  
            kill(processTable[i].pid, SIGKILL);
        } 
    }
}

bool pid_on_process_table(PCB processTable[], int simultaneous, pid_t pid){
    for(int i = 0; i < simultaneous; i++){
        if(processTable[i].pid == pid){  
            return true;
        } 
    }
    return false;
}

# endif