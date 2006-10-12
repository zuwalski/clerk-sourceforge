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
