/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  char_devices.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月16日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月16日 20时18分43秒"
 *                 
 ********************************************************************************/


#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>

/* 确定主设备号 */
//#define DEV_MAJOR		79
#ifndef DEV_MAJOR
#define DEV_MAJOR		0
#endif

int dev_major = DEV_MAJOR;			//主设备号

#define DEV_NAME		"chrdev"	//设备名称

static struct cdev *chrtest_cdev;	//cdev结构体

static struct class *chrdev_class;	//定义一个class用于自动创建类

static char kernel_buf[1024];

#define MIN(a, b) (a < b ? a : b)


/*
 *+------------------------------------------------------------------------------+ 
 *|  实现对应的open/read/write等函数，填入file_operations结构体
 *+------------------------------------------------------------------------------+
 */
static ssize_t chrtest_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
	int		err;
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	err = copy_to_user(buf, kernel_buf, MIN(1024, size));	//内核空间的数据到用户空间上的复制
	return MIN(1024, size);
}

static ssize_t chrtest_drv_write(struct file *file, const char __user *buf, size_t size,loff_t *offset)
{
	int		err;
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	err = copy_from_user(kernel_buf, buf,  MIN(1024,size));		//将buf中的数据复制到写缓存区kernel_buf中,因为用户空间内存不能直接访问内核空间的内存
	return MIN(1024, size);
}

//open和close函数

static int chrtest_drv_open(struct inode *node, struct file *file)
{
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	return 0;
}


static int chrtest_drv_close(struct inode *node, struct file *file)
{
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
	return 0;
}


/*
 *+------------------------------------------------------------------------------+ 
 *|  定义自己的file_operations结构体
 *+------------------------------------------------------------------------------+
 */
static struct file_operations chrtest_fops =
{
	.owner		=	THIS_MODULE,
	.open		=	chrtest_drv_open,
	.read		=	chrtest_drv_read,
	.write		=	chrtest_drv_write,
	.release	=	chrtest_drv_close,
}; 


/*
 *+------------------------------------------------------------------------------+ 
 *|  把file_operations结构体告诉内核register_chrdev
 *|  注册驱动函数：写入口函数，安装驱动程序时就好调用这个入口函数
 *+------------------------------------------------------------------------------+
 */
static int __init chrdev_init(void)
{
	int		result;
	dev_t	devno;	//定义一个dev_t的变量表示设备号

	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	// 字符设备驱动注册流程第二步：分配主次设备号，这里即支持静态指定，也支持动态申请
	if(dev_major != 0) //static
	{
		devno = MKDEV(dev_major, 0);
		result = register_chrdev_region(devno, 1, DEV_NAME);		// /proc/devices/chrdev
	}
	else	//动态申请
	{
		result = alloc_chrdev_region(&devno, 0, 1, DEV_NAME);
		dev_major = MAJOR(devno);		// 获取主设备号
	}

	//若自动分配设备号失败
	if(result < 0)
	{
		printk(KERN_ERR " %s driver can't use major %d\n", DEV_NAME, dev_major);
		return -ENODEV;
	}
	printk(KERN_DEBUG " %s driver use major %d\n", DEV_NAME, dev_major);

	// 字符设备驱动注册流程第三步：分配cdev结构体，这里使用动态申请的方式
	if((chrtest_cdev = cdev_alloc()) == NULL)
	{
		printk(KERN_ERR " %s driver can't alloc for the cdev\n", DEV_NAME);
		unregister_chrdev_region(devno, 1);
		return -ENOMEM;
	}

	// 字符设备驱动注册流程第四步：分配cdev结构体，绑定主次设备号、fops到cdev结构体中，并注册给Linux内核
	chrtest_cdev->owner = THIS_MODULE;	//.owner表示是谁拥有你这个驱动程序
	cdev_init(chrtest_cdev, &chrtest_fops);	//初始化设备
	result  = cdev_add(chrtest_cdev, devno, 1);	//将字符设备注册进内核
	if(result != 0)
	{
		printk(KERN_INFO " %s driver can't register cdev:result=%d\n", DEV_NAME, result);
		goto ERROR;
	}
	printk(KERN_INFO " %s driver can register cdev:result=%d\n", DEV_NAME, result);


	//自动创建设备类型、/dev设备节点
#if 1
	chrdev_class = class_create(THIS_MODULE, DEV_NAME);	//创建设备类型 /sys/class/chrdev
	if(IS_ERR(chrdev_class))
	{
		result = PTR_ERR(chrdev_class);
		goto ERROR;
	}
	device_create(chrdev_class, NULL, MKDEV(dev_major, 0), NULL, DEV_NAME); // /dev/chrdev 注册这个设备节点
#endif

	return 0;
ERROR:
	printk(KERN_ERR " %s driver installed failure.\n", DEV_NAME);
	cdev_del(chrtest_cdev);
	unregister_chrdev_region(devno, 1);
	return result;
}


/*
 *+------------------------------------------------------------------------------+ 
 *|  有入口函数就应该有出口函数：卸载驱动程序时，就会调用这个出口函数
 *+------------------------------------------------------------------------------+
 */
static void __exit chrdev_exit(void)
{
	printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

	//注销设备类型、/dev设备节点
#if 1
	device_destroy(chrdev_class, MKDEV(dev_major, 0));	//注销这个设备节点
	class_destroy(chrdev_class);	//删除这个设备类型
#endif

	cdev_del(chrtest_cdev);	//注销字符设备
	unregister_chrdev_region(MKDEV(dev_major, 0), 1);	//释放设备号

	printk(KERN_ERR " %s driver version 1.0.0 removed!\n", DEV_NAME);
	return;
}


/*
 *+------------------------------------------------------------------------------+ 
 *|  其他完善：提供设备信息，自动创建设备节点
 *+------------------------------------------------------------------------------+
 */

module_init(chrdev_init);
module_exit(chrdev_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("NongJieying <njy_roxy@outlook.com>");


