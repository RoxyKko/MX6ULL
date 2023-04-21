/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  led_gpio.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月21日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月21日 07时37分24秒"
 *                 
 ********************************************************************************/


#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>

#define DEV_NAME		"my_led"	//最后在/dev路径下的设备名称，应用层open的字符串名

/* 设置LED硬件主要序号 */
//#define DEV_MAJOR 79
#ifndef DEV_MAJOR
#define DEV_MAJOR 0
#endif

#define PLATDRV_MAGIC			0x60	//魔术字
#define LED_OFF					_IO (PLATDRV_MAGIC, 0x18)
#define LED_ON					_IO (PLATDRV_MAGIC, 0x19)

static int dev_major = DEV_MAJOR;		/* 主设备号 */

struct led_device {
	dev_t				devid;			/* 设备号 */
	struct cdev			cdev;			/* cdev结构体，字符设备结构体 */
	struct class		*class;			/* 定义一个class用于创造类 */
	struct device		*device;		/* 设备 */
	struct device_node	*node;			/* LED设备节点 */
	struct gpio_desc	*led_gpio;		/* led灯GPIO描述符 */
};

struct led_device led_dev;	// LED设备

/* 字符设备操作函数集 */

static int led_open(struct inode *inode, struct file *file)
{
	file->private_data = &led_dev;	//设置私有数据
	printk(KERN_DEBUG "/dev/led%d opened.\n", led_dev.devid);
	return 0;
}


















