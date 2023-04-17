/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  chrdevbaseApp.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月17日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月17日 00时20分54秒"
 *                 
 ********************************************************************************/


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/*
 * ./chrdevbaseApp -w abc
 * ./chrdevbaseApp -r
 * */

int main (int argc, char **argv)
{
	int		fd;
	char	buf[1024];
	int		len;
	/* 1、判断参数 */
	if(argc < 2)
	{
		printf("Usage: %s -w <string>\n", argv[0]);
		printf("	   %s -r\n", argv[0]);
		return -1;
	}

	/* 2、打开文件 */
	fd = open("/dev/chrdev", O_RDWR);
	if(fd == -1)
	{
		printf("can't open file /dev/chrdev\n");
		return -1;
	}

	/* 3、写文件或读文件 */
	if((strcmp(argv[1], "-w") == 0) && (argc == 3))
	{
		len = strlen(argv[2]) + 1;
		len = len < 1024 ? len : 1024;
		write(fd, argv[2], len);
	}
	else if((strcmp(argv[1], "-r") == 0) && (argc == 2))
	{
		len = read(fd, buf, 1024);
		buf[1023] = '\0';
		printf("APP read : %s\n", buf);
	}
	else
	{
		printf("Usage: %s -w <string>\n", argv[0]);
		printf("	   %s -r\n", argv[0]);
		return -1;
	}

	close(fd);

	return 0;
} 

