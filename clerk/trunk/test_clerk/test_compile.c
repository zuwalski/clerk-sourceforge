/* 
    Clerk application and storage engine.
    Copyright (C) 2008  Lars Szuwalski

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
	TEST Compiler
*/
#include "test.h"
#include "../cle_core/cle_stream.h"
#include "../cle_core/cle_compile.h"
#include <stdio.h>
#include <time.h>


// testhandler w any argument
static void _start2(event_handler* v)
{
	printf(" + start2: ");
}
static void _next2(event_handler* v)
{
	printf(" + next2: ");
}
static void _end2(event_handler* v,cdat c,uint u)
{
	printf(" + end2: %s %d \n",c,u);
}
static void _pop2(event_handler* v)
{
	printf(" + pop2: ");
}
static void _push2(event_handler* v)
{
	printf(" + push2: ");
}
static uint _data2(event_handler* v,cdat c,uint u)
{
	printf("%.*s",u,c);
	return 0;
}
static void _submit2(event_handler* v,task* t,st_ptr* st)
{
	printf(" + submit2: ");
}

// defs
static cle_pipe _test_pipe = {_start2,_next2,_end2,_pop2,_push2,_data2,_submit2};

///////////////////////////////////////////////////////
// TEST Scripts

static char test1[] = 
"$a)"
"	if $a > 1 do"
"		1 + 2 * 3 / $a"
"	elseif $a == 0 do"
"		$a"
"	end";

static char test2[] = 
"$a)"
"	var $b = 0;"
"	while $a > 0 do"
"		var $c = "
"			repeat"
"				$a and $b or $c"
"				break"
"			until $a == 0 end;"
"	"
"		do"
"			var $d = 0;"
"			break"
"		end"
"	end";

static char test3[] = 
"$a,$b)"
"	$a{a,b= 1+1;c{['a' 'b' $b]}} "
"handle a,$c do"
"	$c "
"handle v do"
"	'end'";

static char test4[] = 
"$a,$b)"
"	pipe fun3($a,$b) do "
"		fun3($a)"
"	end"
"	"
"	$b.each a{+*.$1,-$2.$3} do"
"		'each'"
"	end";

static char test5[] = 
"$a,$b)"
"   fun1()"
"	if fun2() do "
"		fun3()"
"	end "
"   b.d = c;"
"   b.d = c d;"
"   var $1 = fun4() d;"
"   var $2 = a b c;"
"   #var $3,$4 = a,b;"
"	";

static char test6[] = 
"$a)"
"	if $a do"
"		$a $a"
"	elseif if $a do 1 else 0 end do"
"	#elseif 0 do\n"
"		$a;"
"		$a $a;"
"		$a"
"	end";


static void _null_to_space(char* src, int len)
{
	while(len > 0)
	{
		if(*src == 0)
			*src = ' ';

		src++;
		len--;
	}
}

static void _do_test(task* t, char* test, int length)
{
	st_ptr dest,src,tmp;

	puts("test\n");

	st_empty(t,&src);
	tmp = src;

	_null_to_space(test,length);
	st_insert(t,&tmp,test,length);

	//puts(test);

	st_empty(t,&dest);
	tmp = dest;
	if(cmp_method(t,&dest,&src,&_test_pipe,0) == 0)
		_rt_dump_function(t,&tmp);
}

void test_compile_c()
{
	task* t = tk_create_task(0,0);

	//_do_test(t,test1,sizeof(test1));
	//_do_test(t,test2,sizeof(test2));
	//_do_test(t,test3,sizeof(test3));
	//_do_test(t,test4,sizeof(test4));
	//_do_test(t,test5,sizeof(test5));
	_do_test(t,test6,sizeof(test6));

	tk_drop_task(t);
}
