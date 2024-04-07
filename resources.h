// CS4760-001SS - Terry Ford Jr. - Project 5 Resource Management - 03/29/2024
// https://github.com/tfordjr/resource-management.git

#ifndef RESOURCES_H
#define RESOURCES_H

#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <fstream>
#include <string>
#include <queue>
#include "pcb.h"

struct Resource{
    int allocated;
    int available;
};

struct Resource resourceTable[NUM_RESOURCES];     // resource table
std::queue<pid_t> resourceQueues[NUM_RESOURCES];  // Queues for each resource

void init_resource_table(Resource resourceTable[]){
    for(int i = 0; i < NUM_RESOURCES; i++){
        resourceTable[i].available = NUM_INSTANCES;
        resourceTable[i].allocated = 0;      
    }
}

void print_resource_table(Resource resourceTable[], int secs, int nanos, std::ostream& outputFile){
    static int next_print_secs = 0;  // static ints used to keep track of each 
    static int next_print_nanos = 0;   // process table print to be done

    if(secs > next_print_secs || secs == next_print_secs && nanos > next_print_nanos){
        std::cout << "OSS PID: " << getpid() << "  SysClockS: " << secs << "  SysClockNano " << nanos << "\nResource Table:\nResource  Allocated  Available\n";
        outputFile << "OSS PID: " << getpid() << "  SysClockS: " << secs << "  SysClockNano " << nanos << "\nResource Table:\nResource  Allocated  Available\n";
        for(int i = 0; i < NUM_RESOURCES; i++){
            std::cout << static_cast<char>(65 + i) << "\t  " << std::to_string(resourceTable[i].allocated) << "\t     " << std::to_string(resourceTable[i].available) << std::endl;
            outputFile << static_cast<char>(65 + i) << "\t  " << std::to_string(resourceTable[i].allocated) << "\t     " << std::to_string(resourceTable[i].available) << std::endl;
        }
        next_print_nanos = next_print_nanos + 500000000;
        if (next_print_nanos >= 1000000000){   // if over 1 billion nanos, add 1 second, sub 1 bil nanos
            next_print_nanos = next_print_nanos - 1000000000;
            next_print_secs++;
        }    
    }
}

int return_PCB_index_of_pid(PCB processTable[], int simultaneous, pid_t pid){
    for (int i = 0; i < simultaneous; i++){  
        if (processTable[i].pid == pid){
            return i;
        }
    }
    perror("resources.h: Error: given pid not found on process table");
    exit(1);
    return -1;
}

void allocate_resources(PCB processTable[], int simultaneous, int resource_index, pid_t pid){
    resourceTable[resource_index].available -= 1;
    resourceTable[resource_index].allocated += 1;
    // LOG ALLOCATION OF RESOURCES SOMEWHERE
    int i = return_PCB_index_of_pid(processTable, simultaneous, pid);
    processTable[i].resourcesHeld[resource_index]++;
    // Notify the process that it has been allocated resources
}

bool request_resources(PCB processTable[], int simultaneous, int resource_index, pid_t pid){
    if (resourceTable[resource_index].available > 0){
        allocate_resources(processTable, simultaneous, resource_index, pid);
        return true;        
    } 
    std::cout << "Insufficient resources available for request." << std::endl;
    resourceQueues[resource_index].push(pid);
    return false;
}

void release_resources(PCB processTable[], int simultaneous, Resource resourceTable[], pid_t killed_pid){ // needs process table to find out
    // find held resources by killed_pid
    int i = return_PCB_index_of_pid(processTable, simultaneous, killed_pid);

    for (int j = 0; j < NUM_RESOURCES; j++){
        resourceTable[j].available += processTable[i].resourcesHeld[j];
        resourceTable[j].allocated -= processTable[i].resourcesHeld[j];
        processTable[i].resourcesHeld[j] = 0;
    }

    // RESOURCES DONE RELEASING HERE


    // If there are processes waiting in the queue for this resource, allocate resources to them
    for (int j = 0; j < NUM_RESOURCES; j++){
        while (!resourceQueues[j].empty() && resourceTable[j].available > 0){                      
            allocate_resources(processTable, j, resourceQueues[j].front()); // Allocate one instance to the waiting process
            resourceQueues[j].pop();
        }
    }
}

int dd_algorithm(PCB processTable[], int simultaneous){   // if deadlock, return resource number, else return 0
    for(int i = 0; i < NUM_RESOURCES; i++){ // for each resource
        int sum = 0;  // sum of instances of a particular resource held by blocked procs
        for(int j = 0; j < simultaneous; j++){  // go through each process 
            if(processTable[j].blocked){  // if process is blocked
                sum += processTable[j].resourcesHeld[i]; 
            }            
        }
        if(sum == NUM_INSTANCES){ // if sum of resource instances held by blocked processes is equal to max number
            return i;  // return resource index number 
        }
    }
    return 0;
}    

void deadlock_detection(PCB processTable[], int simultaneous, Resource resourceTable[], int secs, int nanos){
    static int next_dd_secs = 0;  // used to keep track of next deadlock detection    
    if(secs >= next_dd_secs){
        int deadlocked_resource_index = dd_algorithm(processTable, simultaneous); // returns 0 if no deadlock
        while(deadlocked_resource_index){  // While deadlock
            // kills random pid that is allocated a resource that is fully allocated
            kill(resourceQueues[deadlocked_resource_index].front(), SIGKILL);            
            release_resources(processTable, simultaneous, resourceTable, resourceQueues[deadlocked_resource_index].front()); // release resources held by PID!            
            resourceQueues[deadlocked_resource_index].pop();
            deadlocked_resource_index = dd_algorithm();
        }
        next_dd_secs++;
    }
}

    // dd_algo   for each process in blocked queue, if all other instances of a given Resource
    // are held by blocked processes, then a deadlock exists

    // We will solve it by killing a random blocked process that holds at least one instance 
    // of a resource that whose instances are all allocated
    // then run dd_algo again

    // KEEP STATS OF HOW MANY PROCs KILLED THIS WAY
    // KEEP STATS OF HOW MANY TIMES dd_algorithm is run!

    // SHOULD BE ALMOST THE SAME AS 2nd half of release_resources() but more comprehensive
    // need to check all 
void attempt_process_unblock(PCB processTable[], int simultaneous, Resource resourceTable[]){
    
}

#endif