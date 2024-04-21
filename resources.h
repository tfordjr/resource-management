// CS4760-001SS - Terry Ford Jr. - Project 6 Memory Management - 04/21/2024
// https://github.com/tfordjr/memory-management.git

#ifndef RESOURCES_H
#define RESOURCES_H

#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <fstream>
#include <string>
#include <queue>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "pcb.h"
#include "msgq.h"
#include "rng.h"

struct Resource{
    int allocated;
    int available;
};

void send_msg_to_child(msgbuffer);

struct Resource resourceTable[NUM_RESOURCES];     // resource table
std::queue<pid_t> resourceQueues[NUM_RESOURCES];  // Queues for each resource
int ddAlgoKills = 0;           // globals to keep track for statistics output after execution
int ddAlgoRuns = 0;
int numDeadlocks = 0;
int requestsImmediatelyGranted = 0;
int requestsEventuallyGranted = 0;

std::ofstream outputFile;   // init file object

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
        std::cout << "OSS PID: " << getpid() << "  SysClockS: " << secs << "  SysClockNano " << nanos << "\nResource Table:\nResource  Allocated  Available  Resource Queue Size\n";
        outputFile << "OSS PID: " << getpid() << "  SysClockS: " << secs << "  SysClockNano " << nanos << "\nResource Table:\nResource  Allocated  Available  Resource Queue Size\n";
        for(int i = 0; i < NUM_RESOURCES; i++){
            std::cout << static_cast<char>(65 + i) << "\t  " << std::to_string(resourceTable[i].allocated) << "\t     " << std::to_string(resourceTable[i].available) << "         " << std::to_string(resourceQueues[i].size()) <<std::endl;
            outputFile << static_cast<char>(65 + i) << "\t  " << std::to_string(resourceTable[i].allocated) << "\t     " << std::to_string(resourceTable[i].available) << "         " << std::to_string(resourceQueues[i].size()) <<std::endl;
        }
        std::cout << "Resource Queues (number of resources in each):" << std::endl;
        for(int i = 0; i < NUM_RESOURCES; i++){
            std::cout << static_cast<char>(65 + i) << ": " << std::to_string(resourceQueues[i].size()) << "   ";
        }
        std::cout << "\n";
        next_print_nanos = next_print_nanos + 500000000;
        if (next_print_nanos >= 1000000000){   // if over 1 billion nanos, add 1 second, sub 1 bil nanos
            next_print_nanos = next_print_nanos - 1000000000;
            next_print_secs++;
        }    
    }
}

void remove_pid_from_queue(std::queue<pid_t>& pid_queue, pid_t pid) {
    std::queue<pid_t> temp_queue;

    while (!pid_queue.empty()) {   
        if (pid_queue.front() != pid) {
            temp_queue.push(pid_queue.front());
        }
        pid_queue.pop();
    }

    while (!temp_queue.empty()) {   // move elements back to original queue
        pid_queue.push(temp_queue.front());
        temp_queue.pop();
    }
}

int return_PCB_index_of_pid(PCB processTable[], int simultaneous, pid_t pid){
    for (int i = 0; i < simultaneous; i++){  
        if (processTable[i].pid == pid){
            return i;
        }
    }
    return -1;
}

    //allocate_resources() ALLOCATES UNCONDITIONALLY, MUST BE CAREFUL WHEN WE CALL IT!!!
void allocate_resources(PCB processTable[], int simultaneous, int resource_index, pid_t pid){

    resourceTable[resource_index].available--;  //allocate on resource table
    resourceTable[resource_index].allocated++;
          
    int i = return_PCB_index_of_pid(processTable, simultaneous, pid);
    processTable[i].resourcesHeld[resource_index]++;   // LOG ALLOCATION OF RESOURCES ON PCB  
          
    msgbuffer buf;         // Notify the process that it has been allocated resources
    buf.mtype = pid;
    buf.msgCode = MSG_TYPE_GRANTED;
    buf.resource = resource_index;
    send_msg_to_child(buf);
}

void request_resources(PCB processTable[], int simultaneous, int resource_index, pid_t pid){
    if (resourceTable[resource_index].available > 0){
        allocate_resources(processTable, simultaneous, resource_index, pid);
        requestsImmediatelyGranted++;
        std::cout << "OSS: Granted " << pid << " resource " << static_cast<char>(65 + resource_index) << std::endl;
        return;        
    } 
    std::cout << "OSS: Insufficent resources " << pid << " added to resource queue " << static_cast<char>(65 + resource_index) << std::endl;
    int i = return_PCB_index_of_pid(processTable, simultaneous, pid);

    // SEND MESSAGE LETTING PROC KNOW HE IS BLOCKED
    msgbuffer buf;
    buf.mtype = pid;
    buf.msgCode = MSG_TYPE_BLOCKED;
    buf.resource = resource_index;
    send_msg_to_child(buf);

    processTable[i].blocked = 1;
    resourceQueues[resource_index].push(pid);
}

void release_all_resources(PCB pTable[], int simultaneous, Resource rTable[], pid_t killed_pid){ // needs process table to find out
    int i = return_PCB_index_of_pid(pTable, simultaneous, killed_pid);

    for (int j = 0; j < NUM_RESOURCES; j++){       
        rTable[j].available += pTable[i].resourcesHeld[j];
        rTable[j].allocated -= pTable[i].resourcesHeld[j];
        pTable[i].resourcesHeld[j] = 0;
    }
}

void release_single_resource(PCB processTable[], int simultaneous, Resource resourceTable[], pid_t pid){        
    int i = return_PCB_index_of_pid(processTable, simultaneous, pid);
    if (i == -1)  // Common bug where process termed and old release resource msg
        return;   // for some reason left in msgq, msg release req safely ignored

    for (int j = 0; j < 10; j++){  // 10 tries to release a random resource index
        int randomIndex = generate_random_number(0, (NUM_RESOURCES - 1), getpid());
        if (processTable[i].resourcesHeld[randomIndex] > 0){
            resourceTable[randomIndex].available++;
            resourceTable[randomIndex].allocated--;
            processTable[i].resourcesHeld[randomIndex]--;
            std::cout << "OSS: Decided to release Resource " << static_cast<char>(65 + randomIndex) << " from pid " << pid << std::endl;
            return;
        }
    }

    for (int j = 0; j < NUM_RESOURCES; j++){  // if that fails, we sequentially check
        if (processTable[i].resourcesHeld[j] > 0){ // to release a single resource (not random)
            resourceTable[j].available++;
            resourceTable[j].allocated--;
            processTable[i].resourcesHeld[j]--;
            std::cout << "OSS: Decided to release Resource " << static_cast<char>(65 + j) << " from pid " << pid << std::endl;
            return;
        }
    }
    std::cout << "OSS: determined there were no resources to release from pid " << pid << ", release action resulted in no release..." << std::endl;
}  // If both fail, we conclude the child has no resources to release, move on with no action

pid_t find_pid_with_most_resources(pid_t deadlockedPIDs[], PCB processTable[], int arraySize, int simultaneous) {
    int max_resources = 0;
    pid_t pid_with_most_resources = deadlockedPIDs[0];  // will default to random if none found

    for (int i = 0; i < arraySize; i++) {  // i - deadlockedPIDs index, k - processTable index
        int k = return_PCB_index_of_pid(processTable, simultaneous, deadlockedPIDs[i]); 

        int total_resources = 0;        
        for (int j = 0; j < NUM_RESOURCES; j++) {  // j - iterator through resources
            total_resources += processTable[k].resourcesHeld[j];
        }
        if (total_resources > max_resources) {
            max_resources = total_resources;
            pid_with_most_resources = processTable[k].pid;
        }    
    }
    return pid_with_most_resources;
}

pid_t find_pid_with_least_resources(pid_t deadlockedPIDs[], PCB processTable[], int arraySize, int simultaneous) {
    int min_resources = 999;
    pid_t pid_with_least_resources = deadlockedPIDs[0];  // will default to random if none found

    for (int i = 0; i < arraySize; i++) {  // i - deadlockedPIDs index, k - processTable index
        int k = return_PCB_index_of_pid(processTable, simultaneous, deadlockedPIDs[i]); 

        int total_resources = 0;        
        for (int j = 0; j < NUM_RESOURCES; j++) {  // j - iterator through resources
            total_resources += processTable[k].resourcesHeld[j];
        }
        if (total_resources < min_resources) {
            min_resources = total_resources;
            pid_with_least_resources = processTable[i].pid;
        }
    }    
    return pid_with_least_resources;
}

bool dd_algorithm(PCB processTable[], int simultaneous, Resource resourceTable[], pid_t deadlockedPIDs[], int* index, int* resourceIndex){
    std::cout << "Running dd_algorithm()..." << std::endl;
    ddAlgoRuns++;
    *index = 0;

    struct PCB simProcessTable[20];    // declaration of temp copies of tables, resources
    struct Resource simResourceTable[NUM_RESOURCES];
    std::queue<pid_t> simResourceQueues[NUM_RESOURCES];
    
    for (int i = 0; i < NUM_RESOURCES; i++) {   // Create a local copies        
        simResourceQueues[i] = resourceQueues[i];
        simResourceTable[i] = resourceTable[i];
    }
    for (int i = 0; i < 20; i++) {   // Create a local copies        
        simProcessTable[i] = processTable[i];
    }    

    for (int i = 0; i < simultaneous; i++){   // free all processes not in a blocked queue
        if (!simProcessTable[i].blocked){   // these processes are 100% not deadlocked bc they're not blocked            
            release_all_resources(simProcessTable, simultaneous, simResourceTable, simProcessTable[i].pid);            
        }
    }
                    // THINK SEGFAULT IS BELOW
    int count = 0;
    while(count < 3){   // repeat attempted allocation 3 times to be generous
        for (int i = 0; i < NUM_RESOURCES; i++){ // attempt to allocate free resources
            while (!simResourceQueues[i].empty() && simResourceTable[i].available > 0){                 
                release_all_resources(simProcessTable, simultaneous, simResourceTable, simResourceQueues[i].front());
                
                simResourceQueues[i].pop();                     
            }        
        }
        count++;
    }
    
    for (int i = 0; i < NUM_RESOURCES; i++){ // logging stubborn (probably deadlocked) pids
        while (!simResourceQueues[i].empty()){
            std::cout << "dd_algorithm: DEADLOCKED PID: " << simResourceQueues[i].front() << std::endl;
            if (*index == 0){
                *resourceIndex = i;
            }
            deadlockedPIDs[(*index)++] = simResourceQueues[i].front();            
            simResourceQueues[i].pop();
        }
    }

    if(*index == 0) 
        return false;   // THERE ARE NO DEADLOCKS IN SYSTEM
    return true;
}

void deadlock_detection(PCB processTable[], int simultaneous, Resource resourceTable[], int secs, int nanos){
    static int next_dd_secs = 0;  // used to keep track of next deadlock detection    
    if(secs < next_dd_secs)  // bails if not time for dd() yet
        return;  
    next_dd_secs++;

    pid_t deadlockedPIDs[simultaneous];
    int index = 0, resourceIndex = 0;
    int sameDeadlock = 0;
  
    while(dd_algorithm(processTable, simultaneous, resourceTable, deadlockedPIDs, &index, &resourceIndex)){
        std::cout << "deadlock_detection() found a deadlock! KILLING A PID NOW!" << std::endl;

        // Below are a few options of how to determine which deadlocked pid to terminate (most aren't great options at the moment)

        // int randomIndex = generate_random_number(0, (index - 1), getpid());   // getting segfaults with this code for some reason?
        // pid_t pidDecidedToKill = deadlockedPIDs[randomIndex];   
        
        pid_t pidDecidedToKill = deadlockedPIDs[0];   // No bugs! But not a great way to determine which to terminate

        // pid_t pidDecidedToKill = find_pid_with_most_resources(deadlockedPIDs, processTable, index, simultaneous);
        
        // pid_t pidDecidedToKill = find_pid_with_least_resources(deadlockedPIDs, processTable, index, simultaneous);

        release_all_resources(processTable, simultaneous, resourceTable, pidDecidedToKill);
        update_process_table_of_terminated_child(processTable, pidDecidedToKill, simultaneous);

        kill(pidDecidedToKill, SIGKILL);     // kill random pid
        remove_pid_from_queue(resourceQueues[resourceIndex], pidDecidedToKill);
        
        if(sameDeadlock == 0)  // numDeadlocks tracking
            numDeadlocks++;        
        else if(sameDeadlock > simultaneous){
            std::cout << "deadlock_detection going haywire!" << std::endl;
            kill_all_processes(processTable, simultaneous);
            exit(1);
        }
        sameDeadlock++; 
        ddAlgoKills++;
        index = 0;   
    }
}
    
void attempt_process_unblock(PCB processTable[], int simultaneous, Resource resourceTable[]){   
    for (int j = 0; j < NUM_RESOURCES; j++){
        while (!resourceQueues[j].empty() && resourceTable[j].available > 0){     
            if (!pid_on_process_table(processTable, simultaneous, resourceQueues[j].front())){
                resourceQueues[j].pop();
                continue;
            }                    
            allocate_resources(processTable, simultaneous, j, resourceQueues[j].front()); // Allocate one instance to the waiting process             
            int i = return_PCB_index_of_pid(processTable, simultaneous, resourceQueues[j].front());
            resourceQueues[j].pop();
            processTable[i].blocked = 0;
            requestsEventuallyGranted++;
            std::cout << "OSS: Unblocked " << processTable[i].pid << " granted resource " << static_cast<char>(65 + j) << std::endl;
        }
    }  
}      

#endif