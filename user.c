#include <time.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>

#include "oss.h"

//identifiers for memory, semaphore and queue
static int mid = -1, sid = -1, qid = -1;
static struct simulation *sim = NULL;  //shared memory region

static void shared_lock(){
	struct sembuf semops;

	semops.sem_num = 0;  //use semaphore sn
	semops.sem_flg = 0;  //no flags
	semops.sem_op = -1;  //wait for value to equal 0

	if (semop(sid, &semops, 1) < 0) {
	   perror("semop");
	   exit(EXIT_FAILURE);
	}
}

static void shared_unlock(){
	struct sembuf semops;

	semops.sem_num = 0;  //use semaphore sn
	semops.sem_flg = 0;  //no flags
	semops.sem_op  = 1;  //wait for value to equal 0

	if (semop(sid, &semops, 1) < 0) {
	   perror("semop");
	   exit(EXIT_FAILURE);
	}
}

static int attach_shared(){
	key_t key = ftok(SHARED_PATH, SHARED_KEY+1);
	if(key == -1){
		perror("ftok");
		return 0;
	}

	mid = shmget(key, sizeof(struct simulation), 0);
	if(mid == -1){
		perror("shmget");
		return 0;
	}

	sim = (struct simulation*) shmat(mid, NULL, 0);
  if(sim == (void*)-1){
    perror("shmat");
    return 0;
  }

	key = ftok(SHARED_PATH, SHARED_KEY+2);
	if(key == -1){
		perror("ftok");
		return 0;
	}

  sid = semget(key, 1, 0);
	if(sid == -1){
		perror("semget");
		return 0;
	}

	key = ftok(SHARED_PATH, SHARED_KEY+3);
	if(key == -1){
		perror("ftok");
		return 0;
	}

  qid = msgget(key, 0);
  if(qid == -1){
    perror("msgget");
    return 0;
  }

  return 1;
}

static int access_mem(const int read, const int address){
	struct msgbuf mb;

	mb.mtype = REQ;	//we can't sed mtype 0
	mb.ref[0] = read;
	mb.ref[1] = address;
	mb.ref[2] = getpid();

	if(msgsnd(qid, (void*)&mb, sizeof(mb.ref), 0) == -1){
		perror("msgsnd");
		return -1;
	}

	if(address == -1)	//if we quit
		return 0;		//don't wait for reply

	//receive reply. Will block, until message is received!
	if(msgrcv(qid, (void*)&mb, sizeof(mb.ref), getpid(), 0) == -1){
		perror("msgsnd");
		return -1;
	}

	return mb.ref[0];  //return result
}

static void sim_memory(){

	int check_counter=1000;
	int i, num_accesses = rand() % 5000;
	for(i=0; i < num_accesses; i++){

		const unsigned int address= rand() % PROC_MEM_END;	//generate byte addresss
		const unsigned int read = rand() % 2; //0 is write, 1 is read

		access_mem(read, address);

		if(--check_counter == 0){	//check exit status, when counter is zero

			shared_lock();

			int terminate = sim->terminate;
			shared_unlock();

			check_counter = 1000;

			if(terminate)
				break;
		}
	}

	access_mem(1, -1);	//-1 will tell oss we quit
}

int main(const int argc, char * const argv[]){

	if(!attach_shared())
		return EXIT_FAILURE;

	srand(getpid());

	sim_memory();

	shmdt(sim);

	return EXIT_SUCCESS;
}
