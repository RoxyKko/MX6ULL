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

static int led_release(struct inode *inode, struct file *file)
{
	printk(KERN_DEBUG "/dev/led%d off.\n", led_dev.devid);
	return 0;
}

static void print_led_help(void)
{
	printk("Follow is the ioctl() command for LED driver:\n");
	printk("Turn LED on command			: %u\n", LED_ON);
	printk("TUrn LED off command		: %u\n", LED_OFF);
}

static long led_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch(cmd)
	{
		case LED_ON: //variable case 变量选择
			gpiod_set_value(led_dev.led_gpio, 0);
			break;
		case LED_OFF:
			gpiod_set_value(led_dev.led_gpio, 1);
			break;
		default:
			printk("%s driver don't support ioctl command=%d\n", DEV_NAME, cmd);
			print_led_help();
			return -EINVAL;
	}
	return 0;
}

/* 字符设备操作函数集 */
static struct file_operations led_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.release = led_release,
	.unlocked_ioctl = led_ioctl,
};

/* probe函数实现字符设备的注册和LED灯的初始化 */
static int led_probe(struct platform_device *pdev)
{
	int		result = 0;		//用于保存申请的设备号结果

	printk("\t  match successed  \n");

	memset(&led_dev, 0, sizeof(led_dev));

	/* 获取led的设备树节点 */
	led_dev.led_gpio = gpiod_get(&pdev->dev, "led", 0);// 请求gpio，有请求一定要有释放，否则模块下一次安装将请求失败
	if(IS_ERR(led_dev.led_gpio))
	{
		printk("gpiod request failure\n");
		return PTR_ERR(led_dev.led_gpio);
	}

	result = gpiod_direction_output(led_dev.led_gpio, 0); //设置GPIO的方向为输出，默认电平为低
	if(result != 0)
	{
		printk("gpiod direction output set failure\n");
		return result;
	}

	/* ----------------------------注册 字符设备部分-------------------------------- */
	/* 1. 分配主次设备号，这里既支持静态指定，也支持动态分配 */
	if(dev_major != 0) /* static 静态 */
	{
		led_dev.devid = MKDEV(dev_major, 0); // 宏MKDEV，将主设备号（前12位）和次设备号（后20位）组成32位数
		result = register_chrdev_region(led_dev.devid, 1, DEV_NAME);	/* /proc/devices/my_led */
	}
	else	/* 动态 */
	{
		result = alloc_chrdev_region(&led_dev.devid, 0, 1, DEV_NAME);	/* 动态申请设备号 */
		dev_major = MAJOR(led_dev.devid);		// 宏MAJOR，从32位数中取前12位，获取主设备号
	}

	// 分配设备号失败（包含动态静态）
	if(result < 0)
	{
		printk(" %s driver can't get major %d\n", DEV_NAME, dev_major);
		return result;
	}
	printk(" %s driver use major %d\n", DEV_NAME, dev_major);

	/* 2. 分配cdev结构体，绑定主次设备号、fops到cdev结构体中，并注册给Linux内核 */
	led_dev.cdev.owner = THIS_MODULE;	/* .owner表示谁用于这个驱动程序 */
	cdev_init(&(led_dev.cdev), &led_fops);	/* 初始化cdev，并添加fops */
	result = cdev_add(&(led_dev.cdev), led_dev.devid, 1);	/* 注册给内核，设备数量为1 */

	// 添加失败
	if(result != 0)
	{
		printk("%s driver can't register cder:result = %d\n", DEV_NAME, result);
		goto ERROR;
	}
	printk(" %s driver can register cdev:result=%d\n", DEV_NAME, result);

	/* 3. 创建类，驱动中进行节点创建 */
	led_dev.class = class_create(THIS_MODULE, DEV_NAME); /* /sys/class/my_led 创建类 */
	if(IS_ERR(led_dev.class))
	{
		printk("%s driver create class failure\n", DEV_NAME);
		result = -ENOMEM;
		goto ERROR;
	}

	/* 4. 创建设备 */
	led_dev.device = device_create(led_dev.class, NULL, led_dev.devid, NULL, DEV_NAME); /* /dev/my_led 创建设备节点 */
	if(IS_ERR(led_dev.device))
	{
		result = -ENOMEM;	// 返回错误码，应用空间strerror查看
		goto ERROR;
	}

	return 0;

ERROR:
	printk(KERN_ERR" %s driver installed failure.\n", DEV_NAME);
	cdev_del(&(led_dev.cdev));	// 删除字符设备
	unregister_chrdev_region(led_dev.devid, 1);	//释放主次设备号
	return result;
}

static int led_remove(struct platform_device *pdev)
{
	gpiod_set_value(led_dev.led_gpio, 0);	//低电平关闭灯
	gpiod_put(led_dev.led_gpio);			//释放gpio

	cdev_del(&(led_dev.cdev));				//删除cdev
	unregister_chrdev_region(led_dev.devid, 1); //释放设备号
	device_destroy(led_dev.class, led_dev.devid);	//注销设备
	class_destroy(led_dev.class);			//注销类

	return 0;
}

/*------------------第一部分----------------*/
static const struct of_device_id leds_match_table[] = {
	{.compatible = "my-gpio-leds"},
	{/* sentinel 保护 */}
};

MODULE_DEVICE_TABLE(of, leds_match_table);

/* 定义平台驱动结构体 */
static struct platform_driver gpio_led_driver = {
	.probe		=	led_probe,		//驱动安装是会执行的钩子函数
	.remove		=	led_remove,		//驱动卸载时执行函数
	.driver		=	{				//描述这个驱动的属性
		.name	=	"my_led",		//不建议用的name域
		.owner	=	THIS_MODULE,
		.of_match_table = leds_match_table,
	},
};

/*------------------第二部分----------------*/
/*驱动初始化函数*/
static int __init platdrv_led_init(void)
{
	int		rv = 0;

	rv = platform_driver_register(&gpio_led_driver);	//注册platform的led驱动
	if(rv)
	{
		printk(KERN_ERR "%s:%d: Can't register platform driver %d\n", __FUNCTION__, __LINE__, rv);
		return rv;
	}
	printk("Register LED Platform Driver successfully!\n");
	return 0;
}

/*------------------第三部分----------------*/
/*驱动注销函数*/
static void __exit platdrv_led_exit(void)
{
	printk("%s():%d remove LED platform driver\n", __FUNCTION__, __LINE__);
	platform_driver_unregister(&gpio_led_driver);	//卸载驱动
}

module_init(platdrv_led_init);
module_exit(platdrv_led_exit);

MODULE_AUTHOR("NongJieYing");
MODULE_LICENSE("GPL");

