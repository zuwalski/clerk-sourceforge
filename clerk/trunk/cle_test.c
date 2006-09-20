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
