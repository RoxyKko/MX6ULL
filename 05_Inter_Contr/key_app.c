/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  key_app.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月28日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月28日 00时09分50秒"
 *                 
 ********************************************************************************/


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>


/* 
 *
 * /dev/key0
 *
 * */
#define KEY0VALUE		0xF0
#define INVAKEY			0x00


int main (int argc, char **argv)
{
	int		fd;
	unsigned char keyvalue;

	fd = open("/dev/key0", O_RDWR);
	if(fd < 0)
	{
		printf("can't open file %s\n", strerror(errno));
		return -1;
	}

	while(1)
	{
		read(fd, &keyvalue, sizeof(keyvalue));
		if(keyvalue == KEY0VALUE)
		{
			printf("Key0 Press, value = %#X \r\n", keyvalue);
		}
	}

	close(fd);
	return 0;
} 

