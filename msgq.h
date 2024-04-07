// CS4760-001SS - Terry Ford Jr. - Project 5 Resource Management - 03/29/2024
// https://github.com/tfordjr/resource-management.git

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
	long mtype;			 	  // pid
	int msgCode;			  // msgCode indicates if we're releasing, requesting, or term
	int resource;             // resource requested or releasing
} msgbuffer;

#define MSG_TYPE_BLOCKED 3   // BLOCKED
#define MSG_TYPE_RELEASE 2   // RELEASE
#define MSG_TYPE_REQUEST 1   // REQUEST
#define MSG_TYPE_SUCCESS 0   // TERMINATING
 
#endif