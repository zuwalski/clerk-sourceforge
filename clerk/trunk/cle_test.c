/* 
   Copyright 2005-2006 Lars Szuwalski

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
// ============ TEST / OUTLINE =================
#include "cle_clerk.h"
#include "cle_struct.h"
#include <stdio.h>

static FILE* f;
static void print_struct(page_wrap* pg, const key* me, int ind)
{
	while(1){
		int i;
	
		const char* path = KDATA(me);
  		int l = me->length;
  		int o = me->offset;
  		
		for(i = 0; i < ind; i++)
			fputs("  ",f);

		if(l == 0)
  		{
//  			ptr* pt = (ptr*)me;
//  			page* pg = pt->pg;
//  			fprintf(f,">> (%d) %p(%d/%d) %p >>\n",pt->offset,pg,pg->insert,pg->waste,me);
  			
//	  		if(pg->insert > sizeof(page))
//  				print_struct(FIRSTKEY(pg),ind);
//  			fprintf(f,"<< %p (%d) <<\n",pt->pg,pt->offset);
    	}
  		else
  		{

			fprintf(f,"%s (%s%d/%d) %p",path,
	  		(o < l && *path & (0x80 >> (o & 7)))?"+":"-",o,l,me);
			
	  		if(me->sub){
	  			fputs(" ->\n",f);
				print_struct(pg,GOOFF(pg,me->sub),ind+1);
			}
			else
	  			fputs("\n",f);
		}
  			
		if(!me->next)
			break;
			
		me = GOOFF(pg,me->next);
	}
}

void st_prt_page(st_ptr* pt)
{
	f = stdout;
	fprintf(f,"%p(%d/%d)\n",pt->pg,pt->pg->pg.used,pt->pg->pg.waste);
	print_struct(pt->pg,GOKEY(pt->pg,pt->key),0);
}

/*

clock_t stop0,stop,stop1,start,stop0a;
double duration;

#define DEFRUNS 2000000

int main_0(int argc, char *argv[])
{
	st_ptr pt,tmp;
	task* t = tk_create_task(0);

	st_empty(t,&pt);

	st_prepend(t,&pt,"11",3,0);

	st_prt_page(&pt);

	st_prepend(t,&pt,"22",3,0);

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

*/