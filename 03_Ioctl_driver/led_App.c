/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  led_App.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月22日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月22日 07时21分06秒"
 *                 
 ********************************************************************************/


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>

#define LED_CNT				1
#define DEVNAME_LEN			30

#define PLATDRV_MAGIC		0x60
#define LED_OFF				_IO (PLATDRV_MAGIC, 0x18)
#define LED_ON				_IO (PLATDRV_MAGIC, 0x19)

static inline int msleep(unsigned long ms)
{
	struct timeval tv;
	tv.tv_sec = ms/1000;
	tv.tv_usec = (ms%1000)*1000;
	select(0, NULL, NULL, NULL, &tv);
}


int main (int argc, char **argv)
{
	int				fd[LED_CNT];
	char			dev_name[DEVNAME_LEN];

	memset(dev_name, 0, sizeof(dev_name));
	snprintf(dev_name, sizeof(dev_name), "/dev/my_led");
	fd[LED_CNT] = open(dev_name, O_RDWR, 0755);
	if(fd[LED_CNT] < 0)
	{
		printf("file %s open failure!\n", dev_name);
		goto err;
	}

	printf("open fd: %s [%d] successfully.\n", dev_name, fd[LED_CNT]);

	while(1)
	{
		ioctl(fd[LED_CNT], LED_ON);
		msleep(300);
		ioctl(fd[LED_CNT], LED_OFF);
		msleep(300);
	}

	close(fd[LED_CNT]);
	return 0;

err:
	close(fd[LED_CNT]);
	return -1;
} 

