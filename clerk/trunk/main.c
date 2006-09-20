#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "cle_clerk.h"

void unimplm()
{
	exit(-1);
}

uint page_size = 0;	// TEST
uint resize_count = 0;	// TEST
uint overflow_size = 0;	// TEST


clock_t stop0,stop,stop1,start,stop0a;
double duration;

#define DEFRUNS 2000000

int main_2(int argc, char *argv[])
{
	st_ptr pt,tmp;
	task* t = tk_create_task(0);

	st_empty(t,&pt);

	st_prepend(t,&pt,"11",3);

	st_prt_page(&pt);

	st_prepend(t,&pt,"22",3);

	st_prt_page(&pt);

	system("PAUSE");	
	return 0;
}

int main_1(int argc, char *argv[])
{
	st_ptr pt,tmp;
	it_ptr it;
	task* t = tk_create_task(0);
	uint i,its;
	uint nofound;
	char test[8] = "0000000";

	start = clock();

	st_empty(t,&pt);

	//for(i = 0; i < DEFRUNS; i++)
	//{
	//	st_ptr pt1 = pt;
	//	uint* ui = (uint*)test;
	//	*ui = i;
	//	st_insert(t,&pt1,test,8);
	//}

	stop0 = clock();

	//for(i = 0; i < DEFRUNS; i++)
	//{
	//	st_ptr pt1 = pt;
	//	sprintf_s(test,sizeof(test),"%d",i);
	//	st_insert(t,&pt1,test,8);
	//}
	it_create(&it,&pt);

	for(i = 0; i <DEFRUNS; i++)
	{
		it_new(t,&it,&tmp);
	}

	stop0a = clock();

	nofound = 0;
	//for(i = 0; i < DEFRUNS; i++)
	//{
	//	sprintf_s(test,sizeof(test),"%d",i);
	//	if(!st_exsist(&pt,test,8))
	//		nofound++;
	//		//printf("not found: %d\n",i);
	//}

	stop = clock();

	it_create(&it,&pt);

	its = 0;
	while(it_next(0,&it))
		its++;

	stop1 = clock();
	duration = (double)(stop0 - start) / CLOCKS_PER_SEC;

	printf("nofound: %d\n",nofound);
	printf("runs: %d\ntime: %f\npage_size: %d (%d)\nresize_count: %d\noverflow_size: %d\n",DEFRUNS,duration,page_size,PAGE_SIZE,resize_count,overflow_size);

	printf("Size total: %d Kbytes\n",(PAGE_SIZE * page_size + overflow_size)/1024);

	duration = (double)(stop0a - stop0) / CLOCKS_PER_SEC;
	printf("Insert (sprintf): %f\n",duration);

	duration = (double)(stop - stop0a) / CLOCKS_PER_SEC;
	printf("Lookup: %f\n",duration);

	duration = (double)(stop1 - stop) / CLOCKS_PER_SEC;
	printf("Sort: %f\nEntries: %d\n",duration,its);

	system("PAUSE");	
	return 0;
}

int main(int argc, char *argv[])
{
	st_ptr pt;
	task* t = tk_create_task(0);

	st_empty(t,&pt);

	puts("Input-system running:\n");
	printf("End-code: %d\n", cle_trans(stdin,t,&pt));

	system("PAUSE");	
	return 0;
}