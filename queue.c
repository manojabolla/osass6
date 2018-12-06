#include <stdlib.h>
#include <stddef.h>

#include "queue.h"
#include "util.h"

static struct qfifo fifo;  //queue used by second change to efict pages from memory
static struct qblocked blocked;   //queue of pages, waiting to be loaded by device

int blocked_push(const int cbi, const int addr, const int time_update){
	if(blocked.len == 0){
		blocked.front = 0;
		blocked.end  = 0;
	}else if(blocked.len == CB_COUNT){
		return -1;
	}

	//queue request for device
	struct blocked_item * dn = &blocked.items[blocked.end];
	if(++blocked.end == CB_COUNT)
		blocked.end = 0;
	blocked.len++;

	dn->cbi = cbi;
	dn->addr = addr;

	if(time_update)
		timer_add(dn->time, 15);

  return 0;
}

struct blocked_item * blocked_pop(int clock[2]){
  if(blocked.len == 0 ){  //if device queue is empty
    return NULL;  //empty
  }else{
    //if front request is before current time, return it
    struct blocked_item *dn = &blocked.items[blocked.front];
    if( (dn->time[0] <= clock[0]) &&
		(dn->time[1] <= clock[1])){

	    if(++blocked.front == CB_COUNT){
	      blocked.front = 0;
			}
	    blocked.len--;

	    return dn; //we have a request
    }
  }

  return NULL;  //no request at current time
}

int fifo_push(const int pti, const int cbi){
	//dump_fq("before add");
  if(fifo.front == -1){
    fifo.front = fifo.end = 0;
  }else if(fifo.len == PAGE_COUNT){
	  return -1;
  }

  //queue request for device
  struct fifo_item * pq = &fifo.items[fifo.end];
  if(++fifo.end == PAGE_COUNT)
    fifo.end = 0;
  fifo.len++;

  pq->pti = pti;
  pq->cbi = cbi;

	//dump_fq("after add");
  return 0;
}

//Pop first unreferenced page
struct fifo_item * fifo_pop(struct cb * procs){

	struct fifo_item * pn = NULL;

	if(fifo.len == 0){  //if queue is empty
		return NULL;  //empty
	}else{
		while(1){
			pn = &fifo.items[fifo.front];
			struct page * p = &procs[pn->cbi].pages[pn->pti];

			if(++fifo.front == PAGE_COUNT)  //pop
				fifo.front = 0;
			fifo.len--;

			if(p->referenced == 0) //if this page is referenced
				break;

			p->referenced = 0;  //clear bit

			fifo_push(pn->pti, pn->cbi);	//give the page a second chance

			pn->pti = pn->cbi = -1;
		}
	}
	return pn;  //no request at current time
}

static struct blocked_item * _blocked_pop(){
	struct blocked_item *dn = &blocked.items[blocked.front];

	if(++blocked.front == CB_COUNT)
		blocked.front = 0;

	blocked.len--;

	return dn; //we have a request
}

//Pop first unreferenced page
static struct fifo_item * _fifo_pop(){
  struct fifo_item * pn = NULL;

  if(fifo.len == 0){  //if queue is empty
    return NULL;  //empty
  }else{
     pn = &fifo.items[fifo.front];
	  if(++fifo.front == PAGE_COUNT)  //pop
		fifo.front = 0;
	fifo.len--;
  }

  return pn;  //no request at current time
}

//Clear fifo and blocked queues from process pages
void queues_flush(const int cbi){

	int i;
	for(i=0; i < fifo.len; i++){
		struct fifo_item * pn = _fifo_pop();
		if(cbi == pn->cbi){			//if not cleared process
			pn->pti = pn->cbi = -1;
			i--;
		}else{
			fifo_push(pn->pti, pn->cbi);	//re-add it to fifo
		}
	}

	for(i=0; i < blocked.len; i++){
		const struct blocked_item * dn = _blocked_pop();
		if(dn->cbi == cbi){	//if not cleared process
			i--;
		}else{
			blocked_push(dn->cbi, dn->addr, 0);	//re-add it to fifo
		}
	}
}

void queues_init(){
	fifo.front 	  = fifo.end    = -1;
	blocked.front = blocked.end = -1;
	fifo.len = blocked.len = 0;

	fifo.items 		= (struct fifo_item*)    malloc(sizeof(struct fifo_item)*PAGE_COUNT);
	blocked.items	= (struct blocked_item*) malloc(sizeof(struct blocked_item)*CB_COUNT);
}

void queues_deinit(){
	free(fifo.items);
	free(blocked.items);
}
