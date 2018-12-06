#include <setjmp.h>
#include <string.h>
#include <sys/shm.h>
#include <errno.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include "oss.h"
#include "queue.h"
#include "util.h"

static int procs_max = 0;

enum counters {LINE=0, ACCESS, FAULT, CHILDREN, ID, TERMINATED, NUM_COUNTERS};
static unsigned int counter[NUM_COUNTERS];

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

static void wait_children(){
  int i;
  for(i=0; i < CB_COUNT; i++){
    if(sim->procs[i].pid > 0){
      kill(sim->procs[i].pid, SIGTERM); //signal child to finish
      int status;
      waitpid(sim->procs[i].pid, &status, 0); //wait for child to finish
    }
  }
}

static void destroy_shared(){
	if(sid > 0)
		semctl(sid, 0, IPC_RMID);

	if(qid > 0)
		msgctl(qid, IPC_RMID, NULL);

	if(mid > 0){
		shmdt(sim);
		shmctl(mid, IPC_RMID, NULL);
	}
}

static void deinit(){
	wait_children();
	destroy_shared();
	queues_deinit();
}

static int create_shared(){
	key_t key = ftok(SHARED_PATH, SHARED_KEY+1);
	if(key == -1){
		perror("ftok");
		return 0;
	}

	mid = shmget(key, sizeof(struct simulation), IPC_CREAT | IPC_EXCL | S_IRWXU);
	if(mid == -1){
		perror("shmget");
		return 0;
	}

	sim = (struct simulation*) shmat(mid, NULL, 0);
  if(sim == (void*)-1){
    perror("shmat");
    return 0;
  }

  //clear shared memory region
  bzero(sim, sizeof(struct simulation));

	key = ftok(SHARED_PATH, SHARED_KEY+2);
	if(key == -1){
		perror("ftok");
		return 0;
	}

	sid = semget(key, 1, IPC_CREAT | IPC_EXCL | S_IRWXU);
	if(sid == -1){
		perror("semget");
		return 0;
	}

	//initialize semaphore as unlocked
	union semun un;
	un.val = 1;  //sem is unlocked
	if(semctl(sid, 0, SETVAL, un) ==-1){
		perror("sid");
		return 0;
	}

	key = ftok(SHARED_PATH, SHARED_KEY+3);
	if(key == -1){
		perror("ftok");
		return 0;
	}

  qid = msgget(key, IPC_CREAT | IPC_EXCL | O_RDWR | S_IRWXU);
  if(qid == -1){
    perror("msgget");
    return 0;
  }

  return 1;
}

static void signal_terminate(){
	//shared_lock();	//don't lock, since we may be holding the lock!
	sim->terminate = 1;	//number of processes to terminate
	//shared_unlock();
}

static void signal_handler(const int sig){
  if((sig == SIGTERM) || (sig == SIGINT)){
    signal_terminate();
	destroy_shared();
    exit(0);  //stop
  }
}

static int fork_user(){
	if(counter[CHILDREN] >= procs_max)
		return 0;

	//check bit vector if we have free PCB
	int cbi;
	for(cbi=0; cbi < CB_COUNT; cbi++)
		if(sim->procs[cbi].pid == 0)
			break;

	if(cbi == CB_COUNT){  //no PCBs available
		//fprintf(stderr ,"Error: No cb available\n");
		return EXIT_SUCCESS;
	}

	int rc = EXIT_SUCCESS;

	pid_t pid = fork();
	if(pid == -1){
		perror("fork");
		rc = EXIT_FAILURE;
	}else if(pid == 0){
		execl("./user", "./user", NULL);  //run the user program
		perror("execl");
		exit(1);
	}else{
		struct cb * cb = &sim->procs[cbi];
		counter[CHILDREN]++; //total number of processes created
		cb->pid = pid;
		cb->id  = counter[ID]++;
		printf("Master: Generating process with PID %u at time %i:%i\n",
				cb->id, sim->clock[0], sim->clock[1]);
		counter[LINE]++;
	}

	return rc;
}

static void advance_clock(const int nsec){

	const int new_child = rand() % 2;
	static int prev_child[2];	//time last fork was done

	//shared_lock();

	sim->clock[0] += 1;
	timer_add(sim->clock, nsec);

	//shared_unlock();

	//check if we need to fork
	if(sim->clock[0] >= (prev_child[0] + new_child)){

		prev_child[0] = sim->clock[0];
		prev_child[1] = sim->clock[1];

		fork_user();			//make a new process
	}
}

static void fr_reset(struct frame *fr){
  fr->status = FREE;
  fr->pti = fr->cbi = -1;
}

static void cb_reset(struct cb * cb){
	cb->pid = 0;

	int i;
	for(i=0; i < PROC_PAGES; i++){
		struct page *p = &cb->pages[i];
		if(p->fti >= 0){
			fr_reset(&sim->frames[p->fti]);
			p->fti = -1;   //no hardware address (not in memory)
		}
		p->referenced = 0;  //not referenced
	}
}

static void clear_terminated(const int cbi){

	shared_lock();

	cb_reset(&sim->procs[cbi]);
	queues_flush(cbi);

	shared_unlock();
}

static void report_layout(){
	int i;
	printf("Master: Current memory layout at time %i:%i is:\n", sim->clock[0], sim->clock[1]);

	for(i=0; i < FRAME_COUNT; i++){
		switch(sim->frames[i].status){
			case FREE:	putchar('.');	break;
			case DIRTY:	putchar('D');	break;
			case USED:	putchar('U');	break;
		}
	}
	putchar('\n');

	//second line is reference bit status
	for(i=0; i < FRAME_COUNT; i++){
		struct frame * fr = &sim->frames[i];        //frame
		if(fr->pti == -1){
			putchar('.');
		}else{
			if(sim->procs[fr->cbi].pages[fr->pti].referenced){
				putchar('1');
			}else{
				putchar('0');
			}
		}
	}
	putchar('\n');
}

static int mem_fault(struct cb * cb, const int pti){
  int fti;

	counter[FAULT]++;

  //search for a free frame to load page in
  for(fti=0; fti < FRAME_COUNT; fti++){
    if(sim->frames[fti].pti == -1){  //if frame is not used by a page
      counter[LINE]++; printf("Master: Using free frame %d for P%d page %d\n", fti, cb->id, pti);
      return fti; //return its index
    }
  }

  //if we don't have a free frame, we have to evict some other page
  struct fifo_item * pn = fifo_pop(sim->procs); //pop the first unreferenced page from fifo queue
  if(pn == NULL){ //this should not happen
    fprintf(stderr, "BUG: pop on empty fifo\n");
    fflush(stdout);
	deinit();
    exit(EXIT_FAILURE);
  }

  struct page * p = &sim->procs[pn->cbi].pages[pn->pti];

  if(p->fti < 0){
	  fprintf(stderr, "BUG4: poped empty page\n");
	  fflush(stdout);
	  deinit();
	  exit(EXIT_FAILURE);
  }

  struct frame * fr = &sim->frames[p->fti];
  struct cb * oldcb = &sim->procs[fr->cbi];
  counter[LINE]++; printf("Master: Clearing frame %d and swapping P%d page %d for P%d page %d\n",
    p->fti, oldcb->id, fr->pti, cb->id, pti);

  fti = p->fti;
  fr_reset(&sim->frames[p->fti]);
  p->fti = -1;

  return fti;  //return index of free frame
}

static enum access_status mem_load(const int cbi, const int pti, const int addr){
  enum access_status rv;

  if(pti > PROC_PAGES){
		fprintf(stderr, "Error: Page index > %d\n", PROC_PAGES);
		return FAILED;
	}

  struct cb * cb = &sim->procs[cbi];
  struct page * p = &cb->pages[pti];

  if(p->fti < 0){	//if page is not in memory
	counter[LINE]++; printf("Master: Address %d is not in a frame, pagefault\n", addr);

	p->fti = mem_fault(cb, pti); //get frame for page, so we can load it into memory

	p->referenced = 1;  //after page is loaded into memory, raise the ref bit ( give page a second chance )
	sim->frames[p->fti].status = USED;
	sim->frames[p->fti].pti = pti;
	sim->frames[p->fti].cbi = cbi;

	rv = (blocked_push(cbi, addr, 1) == -1) ? FAILED : BLOCKED;

  }else{ //no page fault
    timer_add(sim->clock, 10);	//add 10ns to clock
    rv = SUCCESS;
  }

  return rv;
}

static enum access_status _read(const int cbi, const int vaddr){
	enum access_status rv;
	struct cb * cb = &sim->procs[cbi];

	counter[LINE]++; printf("Master: P%d requesting read of address %d at time %i:%i\n",
		cb->id, vaddr, sim->clock[0], sim->clock[1]);

	const int pti = vaddr / (PAGE_SIZE*1024);
	rv = mem_load(cbi, pti, vaddr);

	if(rv == SUCCESS){
		struct page * p = &cb->pages[pti];
		counter[LINE]++; printf("Master: Address %d in frame %d, giving data to P%d at time %i:%i\n",
			vaddr, p->fti, cb->id, sim->clock[0], sim->clock[1]);
	}

  return rv;
}

static enum access_status _write(const int cbi, const int vaddr){
	enum access_status rv;

	struct cb * cb = &sim->procs[cbi];
	counter[LINE]++; printf("Master: P%d requesting write of address %d at time %i:%i\n",
		cb->id, vaddr, sim->clock[0], sim->clock[1]);

	const int pti     = vaddr / (PAGE_SIZE*1024);
	rv = mem_load(cbi, pti, vaddr);

	if(rv == SUCCESS){
		struct page * p = &cb->pages[pti];
		counter[LINE]++; printf("Master: Address %d in frame %d, writing data to frame at time %i:%i\n",
			vaddr, p->fti, sim->clock[0], sim->clock[1]);

		struct frame * fr = &sim->frames[p->fti];
		if(fr->status != DIRTY){
			counter[LINE]++; printf("Master: Dirty bit of frame %d set, adding additional time to the clock\n", p->fti);
		}
	}

	return rv;
}

static int get_ci(const int pid){
	int i;
	for(i=0; i < CB_COUNT; i++){
		if(sim->procs[i].pid == pid){
			return i;
		}
	}
	return -1;
}

static int manage_memory(){

	struct msgbuf mb;
	const int mtype = -1*MT_COUNT;	//get any message sent by user processes

	//get one request for read/write
	if(msgrcv(qid, (void*)&mb, sizeof(mb.ref), mtype, IPC_NOWAIT) == -1){
		if(errno != ENOMSG){
			perror("msgrcv");
			return -1;
		}
		return 0; //no messages
	}

	//process request
	const int read = mb.ref[0]; //READ or WRITE
	const int addr = mb.ref[1]; //virtual address
	const int cbi = get_ci(mb.ref[2]);  //sim->procs[cbi]

	if(addr == -1){
		clear_terminated(cbi);
		return 0;	//no message, user quits
	}

	enum access_status res = (read) ? _write(cbi, addr)
									: _read( cbi, addr);

	if(res != BLOCKED){
		mb.mtype = sim->procs[cbi].pid;
		mb.ref[0] = res;
		mb.ref[1] = 0; //unused for now

		if(msgsnd(qid, (void*)&mb, sizeof(mb.ref), 0) == -1){
			perror("msgsnd");
			return -1;
		}
	}

	if((++counter[ACCESS] % 100) == 0)  //on each 100 memory accesses
		report_layout();

	return 0;
}

static int manage_blocked(){
	struct msgbuf mb;

	//check dev q, and unblock all request before current time
	struct blocked_item *dn = blocked_pop(sim->clock);
	while(dn){

		struct cb * cb = &sim->procs[dn->cbi];
		const int pti     = dn->addr / (PAGE_SIZE*1024);

		//page is set up. Just send reply to unblock process
		mb.mtype = cb->pid;
		mb.ref[0] = (fifo_push(pti, dn->cbi) == -1) ? FAILED : SUCCESS;
		mb.ref[1] = 0; //unused for now
		mb.ref[2] = 0;

		if(msgsnd(qid, (void*)&mb, sizeof(mb.ref), 0) == -1){
			perror("msgsnd");
			return -1;
		}

		counter[LINE]++; printf("Master: Indicating to P%d that write has happened to address %d\n", cb->id, dn->addr);
		dn = blocked_pop(sim->clock); //get next request
	}

	return 0;
}

static int check_log_size(){
  static int redirected = 0;

  if(redirected)
	return 0;

  if(counter[LINE] >= 100000){	//if log file is larger thatn 10 000 lines
    fprintf(stderr, "Warning: Log file is full. Stopping output\n");
    stdout = freopen("/dev/null", "w", stdout);
    if(stdout == NULL){
      perror("fopen");
      return 1;
    }
    redirected = 1;
  }
  return 0;
}

//simulate a process scheduler
static void simulate(const int procs_max){

	while(counter[CHILDREN] < procs_max){
		advance_clock(rand() % 1000);	//update simulation time
		manage_memory();
		manage_blocked();

		if(check_log_size() != 0)
			break;
	}

	printf("Master: Finished at time %i:%i\n", sim->clock[0], sim->clock[1]);
	printf("Statistics:\n");
	printf("Total requests: %u\n", counter[ACCESS]);
	printf("Memory accesses/s: %.2f\n", (float) counter[ACCESS] / sim->clock[1]);
	printf("Page fauls per memory access: %.2f\n", (float)counter[FAULT] / counter[ACCESS]);

	fflush(stdout);
}

static void init(){

	int i;
	srand(getpid());

	bzero(counter, sizeof(int)*NUM_COUNTERS);

	sim->terminate = 0;

	//clear the fifo queue of pages
	queues_init();

	//clear page table
	for(i=0; i < CB_COUNT; i++)
		cb_reset(&sim->procs[i]);

	//clear frame table
	for(i=0; i < FRAME_COUNT; i++)
		fr_reset(&sim->frames[i]);
}

int main(const int argc, char * const argv[]){

	signal(SIGINT,  signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGCHLD, SIG_IGN);	       //skip child signals

	//parse program options
	if(argc != 2){
		printf("Usage: %s <number of processes>\n", argv[0]);
		return EXIT_FAILURE;
	}

	if(atoi(argv[1]) < 0){ //if above hard limit
	  fprintf(stderr, "Error: Invalid number of processes\n");
	  return EXIT_FAILURE;
	}
	procs_max = atoi(argv[1]);

	freopen("oss.log", "w", stdout);

	if(!create_shared())
		return EXIT_FAILURE;

	init();
	simulate(procs_max);
	deinit();

	return EXIT_SUCCESS;
}
