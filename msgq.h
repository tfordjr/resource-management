// CS4760-001SS - Terry Ford Jr. - Project 4 OSS Scheduler - 03/08/2024
// https://github.com/tfordjr/oss-scheduler.git

#ifndef MSGQ_H
#define MSGQ_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>

#define MSGQ_FILE_PATH "msgq.txt"
#define MSGQ_PROJ_ID 65
#define PERMS 0644

typedef struct msgbuffer {
	long mtype;			 	 // pid
    char message[100];   	 // string message
	int time_slice_used; 	 // time used in nanos (int sufficient for this as max quantum is 40ms)
	int blocked_until_secs;	 // time when process becomes unblocked
	int blocked_until_nanos;
	int msgCode;			  // process state code
} msgbuffer;

#define MSG_TYPE_BLOCKED 2   // BLOCKED
#define MSG_TYPE_RUNNING 1   // STILL RUNNING
#define MSG_TYPE_SUCCESS 0   // TERMINATING

#endif