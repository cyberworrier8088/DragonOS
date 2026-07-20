/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"

void QG_Tick(double duration)
{
	Host_Frame(duration);
}

extern void print_serial(const char* str);
extern void* malloc(size_t size);

void QG_Create(int argc, char *argv[])
{
	static quakeparms_t    parms;

    print_serial("[Quake] Inside QG_Create...\n");

	/* 16MB: the 8MB minimum causes constant surface/model cache eviction
	 * (Cache_Alloc thrash) which shows up as periodic frame hitches. */
	parms.memsize = 16*1024*1024;
	parms.membase = malloc (parms.memsize);
	parms.basedir = ".";

    print_serial("[Quake] Calling COM_InitArgv...\n");
	COM_InitArgv (argc, argv);

	parms.argc = com_argc;
	parms.argv = com_argv;

    print_serial("[Quake] Calling Host_Init...\n");
	Host_Init (&parms);


}
