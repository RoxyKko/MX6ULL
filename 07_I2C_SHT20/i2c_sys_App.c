/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  i2c_sys_App.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月30日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月30日 01时04分30秒"
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

int sht20_get_temp_humi(float *temp, float *humi);

int main(int argc, char **agrv)
{
    float   temp = 0;
    float   humi = 0;

    if(sht20_get_temp_humi(&temp, &humi) < 0)
    {
        printf("ERROR: sht20 get temp and humi failure!\n");
        return -1;
    }
    printf("sht20 get temp: %f 'C\n", temp);
    printf("sht20 get humi: %f % \n", humi);

    return 0;
}

int sht20_get_temp_humi(float *temp, float *humi)
{
    const char      *sht20_path = "/sys/class/sht20/sht20/";
    char            ds_path[50];
    char            buf[128];
    DIR             *dirp = NULL;
    int             fd = -1;
    char            *ptr_begin = NULL;
    char            *ptr_end   = NULL;
    char            data_buf[16];
    int             rv = 0;

    if(!temp || !humi)
    {
        return -1;
    }

    if((dirp = opendir(sht20_path)) == NULL)
    {
        printf("opendir error:%s\n", strerror(errno));
        return -2;
    }

    snprintf(ds_path, sizeof(ds_path), "%stemp_humi", sht20_path);/*my_sht20_driver*/

    if( (fd = open(ds_path, O_RDONLY)) < 0 )
    {
        printf("open %s error: %s\n",ds_path, strerror(errno));
        return -3;
    }

    if(read(fd, buf, sizeof(buf)) < 0)
    {
        printf("read %s error: %s\n", ds_path, strerror(errno));
        rv = -5;
        goto cleanup;
    }

    ptr_begin = strstr(buf, "temp=");
    ptr_end = strstr(buf, ",");

    if( !ptr_begin || !ptr_end )
    {
        printf("ERROR: Can not get temperature\n");
        rv = -6;
        goto cleanup;
    }
    else
    {
        /* 因为此时ptr是指向 "temp="字符串的地址(即't'的地址)，那跳过5个字节(t=)后面的就是采样温度值 */
         ptr_begin += strlen("temp=");
         memcpy(data_buf, ptr_begin, ptr_end - ptr_begin);
    }

    *temp = -46.85 + 175.72/65536 * atof(data_buf);

    ptr_begin = strstr(buf, "humi=");
    ptr_begin += strlen("humi=");

    *humi = -6.0  + 125.0 /65536 * atof(ptr_begin);

cleanup: 
    close(fd);
    return rv;

}
