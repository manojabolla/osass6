#include "oss.h"

struct fifo_item{
	int pti;	//page index in process PT
	int cbi;	//process identifier
};

struct blocked_item{
	int cbi;	//process identifier
	int addr;
	int time[2];
};

struct qfifo{
	struct fifo_item * items;
	int front,end;	//index of first and last element in items[]
	int len;
};

struct qblocked{
	struct blocked_item * items;
	int front,end;	//index of first and last element in items[]
	int len;
};

struct fifo_item * fifo_pop(struct cb * procs);
int fifo_push(const int pti, const int cbi);

struct blocked_item * blocked_pop();
int blocked_push(const int cbi, const int addr, const int time_update);

void queues_flush(const int cbi);
void queues_init();
void queues_deinit();
