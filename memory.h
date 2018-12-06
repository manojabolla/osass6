#ifndef MEMORY_H
#define MEMORY_H

//values are in KB
#define PAGE_SIZE	1
#define MEM_SIZE 	256
#define PROC_PAGES	32

#define PROC_MEM_END (PROC_PAGES * PAGE_SIZE * 1024)
#define PAGE_COUNT MEM_SIZE / PAGE_SIZE
#define FRAME_COUNT 128

enum frame_status { FREE=0, DIRTY, USED};
enum access_status{ SUCCESS=0, FAILED, BLOCKED};

struct page {
	int fti;									//frame/hardware index
	unsigned char referenced;	//reference bit ( for second chance )
};

struct frame{
	int pti;					//index of page, using the frame
	int cbi;
	enum frame_status status;
};

#endif
