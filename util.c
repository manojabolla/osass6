#include "util.h"

void timer_add(int x[2], const int nsec){
	x[1] += nsec;
	if(x[1] > 1000000000){
		x[0]++;
		x[1] -= 1000000000;
	}
}
