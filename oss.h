#ifndef OSS_H
#define OSS_H

#include "memory.h"

#define CB_COUNT 18	//control block count

struct msgbuf{
	long mtype;
	int ref[3];	//read/write, address, pid
};

enum msg_types{	REQ=1, ANS, QUIT, MT_COUNT};	//request, answer, quit

//path used in ftok
#define SHARED_PATH "/tmp"
#define SHARED_KEY 123

struct cb{	//control block for a process
	int pid;
	int id;	//process id
	struct page	  pages[PROC_PAGES];		//page table
};

struct simulation{
	int terminate;			//set to 1, to signal children to quit
	int clock[2];

	struct cb 	   procs[CB_COUNT];
	struct frame  frames[FRAME_COUNT];	//frame table
};

//needs to be defined for semctl (copied from man 2 semctl)
union semun {
   int              val;    /* Value for SETVAL */
   struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
   unsigned short  *array;  /* Array for GETALL, SETALL */
   struct seminfo  *__buf;  /* Buffer for IPC_INFO
							   (Linux-specific) */
};

#endif
