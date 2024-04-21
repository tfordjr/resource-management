// CS4760-001SS - Terry Ford Jr. - Project 6 Memory Management - 04/21/2024
// https://github.com/tfordjr/memory-management.git

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
	long mtype;			 	  // recipient's pid
	int msgCode;			  // msgCode indicates if we're releasing, requesting, or term
	int resource;             // resource requested or releasing
	pid_t sender;
} msgbuffer;

#define MSG_TYPE_GRANTED 4   // GRANTED
#define MSG_TYPE_BLOCKED 3   // BLOCKED
#define MSG_TYPE_RELEASE 2   // RELEASE
#define MSG_TYPE_REQUEST 1   // REQUEST
 
#endif