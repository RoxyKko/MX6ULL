/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  w1_sys_App.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月29日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月29日 07时04分11秒"
 *                 
 ********************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <errno.h>

int ds18b20_get_temperature(float *temp);


int main (int argc, char **argv)
{
	float		temp;

	if(ds18b20_get_temperature(&temp) < 0)
	{
		printf("DS18B20 get temperature failure!\n");
		return -1;
	}

	printf("DS18B20 get temperature: %.4f 'C\n", temp);

	return 0;
} 

int ds18b20_get_temperature(float *temp)
{
	const char		*w1_path = "/sys/class/w1_ds18b20/w1_ds18b20/"; // my_ds18b20_driver
	char			ds_path[50]; /* DS18B20 采样文件路径 */
	char            buf[128];    /* read() 读数据存储 buffer */
	DIR            *dirp;        /* opendir()打开的文件夹句柄 */
	int             fd =-1;      /* open()打开文件的文件描述符 */
	char           *ptr;         /* 一个字符指针，用来字符串处理 */
	int             rv = 0;      /* 函数返回值，默认设置为成功返回(0) */

	if( !temp )
    {
        return -1;
    }

	if((dirp = opendir(w1_path)) == NULL)
    {
        printf("opendir error: %s\n", strerror(errno));
        return -2;
    }

	snprintf(ds_path, sizeof(ds_path), "%s/temp", w1_path);/*my_ds18b20_driver*/

	// 打开 DS18B20 的采样文件
	if( (fd=open(ds_path, O_RDONLY)) < 0 )
    {
        printf("open %s error: %s\n", ds_path, strerror(errno));
        return -4;
    }

	// 读取文件中的内容将会触发 DS18B20温度传感器采样
	if(read(fd, buf, sizeof(buf)) < 0)
	{
		printf("read %s error: %s\n", ds_path, strerror(errno));
		rv = -5;
        goto cleanup;
	}

	/* 采样温度值是在字符串"t="后面，这里我们从buf中找到"t="字符串的位置并保存到ptr指针中 */
	ptr = strstr(buf, "temp = ");
	if( !ptr )
    {
        printf("ERROR: Can not get temperature\n");
        rv = -6;
        goto cleanup;
    }
	ptr+=7;	/* 跳过"temp = "字符串 */

	/* 接下来我们使用 atof() 函数将采样温度值字符串形式，转化成 float 类型。*/
    *temp = atof(ptr)/10000;

cleanup: 
    close(fd);
    return rv;
}
