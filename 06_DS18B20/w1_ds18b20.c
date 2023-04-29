/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  w1_ds18b20.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月29日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月29日 02时06分43秒"
 *                 
 ********************************************************************************/

#include <linux/module.h>			// module必须的头文件
#include <linux/init.h>				// module必须的头文件
#include <linux/fs.h>				// file_operations,用于联系系统调用和驱动程序
#include <linux/errno.h>			// ENODEV,ENOMEM存放的头文件
#include <linux/kernel.h>			// printk(),内核打印函数
#include <linux/device.h>			// 用于自动生成设备节点的函数头文件
#include <linux/gpio.h>				// gpio相关函数
#include <linux/gpio/consumer.h>	// gpiod相关函数
#include <linux/of_gpio.h>			// gpio子系统相关函数
#include <linux/uaccess.h>			// copy_to_user函数头文件
#include <linux/timer.h>			// 定时器相关函数
#include <linux/cdev.h>				// cdev相关函数
#include <linux/platform_device.h>	// platform相关结构体
#include <linux/delay.h>


#define DEV_NAME				"w1_ds18b20"	// 最后在/dev路径下的设备名称，应用层open的字符串名
#define DEV_CNT					1				// 设备数量

#ifndef DEV_MAJOR
#define DEV_MAJOR				0
#endif

#define LOW						0
#define HIGH					1

/* DS18B20 ROM命令 */
#define Read_ROM				0x33	// 该命令只能在总线上有一个从机时使用
#define Skip_ROM				0xCC	// 不送出任何ROM码信息

/* DS18B20 FUNC命令 */
#define Convert_T				0x44	// 启动一个单一的温度转换。
#define Read_Data				0xBE	// 主机读取暂存寄存器的内容

#define CRC_MODEL				0x31
#define CRC_SUCCESS				0
#define CRC_FAIL				1

static int dev_major = DEV_MAJOR;		/* 主设备号 */

/* 存放w1的私有属性 */
struct gpio_w1_priv {
	struct cdev			cdev;			// cdev结构体
	struct class		*dev_class;		// 自动创建设备节点的类
	struct device		*dev;
	spinlock_t			lock;
};

struct gpio_desc		*w1_gpiod;		// gpio描述符

/* DS18B20 接收发送 */
#define DS18B20_In_Init()		gpiod_direction_input(w1_gpiod)
#define DS18B20_Out_Init()		gpiod_direction_output(w1_gpiod, HIGH)
#define DS18B20_DQ_IN()			gpiod_get_value(w1_gpiod)
#define DS18B20_DQ_OUT(N)		gpiod_set_value(w1_gpiod, N)

/**
 * @name: parser_dt_init_w1
 * @description: 解析设备树，初始化w1硬件io
 * @param {platform_device} *pdev
 * @return  0 successfully , !0 failure
 */
static int parser_dt_init_w1(struct platform_device *pdev)
{
	struct device 		*dev = &pdev->dev;				// 设备指针
	struct gpio_w1_priv *priv = NULL;
	struct gpio_desc	*gpiod;							// gpiod结构体

	/* 为wq私有属性分配存储空间 */
	priv = devm_kzalloc(dev, sizeof(struct gpio_w1_priv), GFP_KERNEL);
	if(!priv)
	{
		return -ENOMEM;
	}

	/* 获取w1的设备树节点 */
	gpiod = gpiod_get(dev, "w1", 0);	//请求gpio，有请求一定要有释放，否则模块下一次安装将请求失败
	if(IS_ERR(gpiod))
	{
		printk("gpiod request failure\n");
		return PTR_ERR(gpiod);
	}

	/* 将gpiod传给结构体 */
	w1_gpiod = gpiod;

	dev_info(&pdev->dev, "parser dts got valid w1\n");

	platform_set_drvdata(pdev, priv);	// 设置私有数据，方便其他函数操作

	return 0;
}

static int w1_delay_parm = 1;		// 

/**
 * @name: w1_delay(unsigned long tm)
 * @description: 微秒延时函数
 * @param {unsigned long} tm 延时的微秒
 * @return {*}
 */
static void w1_delay(unsigned long tm)
{
	udelay(tm * w1_delay_parm);
}

/**
 * @name: DS18B20_start(void)
 * @description: 主机发送起始信号，主机拉低 >=480us，释放总线（主机拉高），等待15~60us
 * @return 0 successfully , !0 failure
 */
static int DS18B20_start(void)
{
	int rv;

	/* 主机设置为输出 */
	DS18B20_Out_Init();		// 设置gpio方向为输出，默认为高电平

	/* 主机拉低 >= 480us */
	DS18B20_DQ_OUT(LOW);
	w1_delay(480);

	/* 主机拉高 15~60us */
	DS18B20_DQ_OUT(HIGH);
	w1_delay(70);

	/* 主机设置为输入 */
	DS18B20_In_Init();

	/* 若总线被从机拉低则说明设备发送了相应信号：60~240us */
	rv = DS18B20_DQ_IN();
	if(rv != LOW)
	{
		printk("%s():%d DS18B20 not response\n", __FUNCTION__, __LINE__);
		return EFAULT;
	}

	printk("%s():%d DS18B20 response\n", __FUNCTION__, __LINE__);
	w1_delay(10);

	/* 再次转回输出模式 */
	DS18B20_Out_Init();

	/* 释放总线 */
	DS18B20_DQ_OUT(HIGH);

	return rv;
}

/**
 * @name: 
 * @description: 主机读取一个位,整个读周期最少需要60us，启动读开始信号后必须15us内读取IO电平，否则就会被上拉拉高
 * @return uint8_t bit 
 */
static uint8_t DS18B20_readBit(void)
{
	uint8_t bit = 0;
	/* 主机再设置重输出模式准备开始读数据 */
	DS18B20_Out_Init();		// 设置gpio方向为输出，默认为高电平

	/* 主机拉低 >= 1us ,表示开始读数据 */
	DS18B20_DQ_OUT(LOW);
	w1_delay(2);

	/* 切换回输入模式 */
	DS18B20_In_Init();
	w1_delay(10);

	/* 获取bit数据 */
	bit = DS18B20_DQ_IN();

	/* 2us+10us+50us = 62us > 60us 为一个周期 */
	w1_delay(50);

	return bit;
}

/**
 * @name: static uint8_t DS18B20_readByte(void)
 * @description: 从DS18B20上读取一个字节
 * @return uint8_t Bety 
 */
static uint8_t DS18B20_readByte(void)
{
	uint8_t		i,Bety=0;
	uint8_t		bit;

	for(i=0; i<8; i++)
	{
		bit = DS18B20_readBit();
		/* 若bit为1，则将0x01左移i位，与bety进行位或运算，位或运算中，遇1为1，遇0为原本的数 */
		if(bit)
			Bety |= (0x01 << i);
	}

	printk("%s():%d DS18B20 readByte %x \n", __FUNCTION__, __LINE__, Bety);

	return Bety;
}

/**
 * @name: static void DS18B20_writeBit(unsigned char bit)
 * @description: ds18b20 写一个位
 * @param {unsigned char} bit 写入的位
 * @return {*}
 */
static void DS18B20_writeBit(unsigned char bit)
{
	DS18B20_Out_Init();

	/* 判断bit,防止误输入 */
	bit = bit > 1 ? 1 : bit; 
	w1_delay(50);

	/* 先拉低>1us */
	DS18B20_DQ_OUT(LOW);
	w1_delay(2);

	/* 若为写入逻辑1 ，在先拉低>1us后15us内拉高总线 */
	/* 若为写入逻辑0 ，要拉低并保持低电平 2us + x = 60us~120us，然后再释放总线 */
	DS18B20_DQ_OUT(bit);
	w1_delay(60);

	/* 若写入逻辑0，这里是释放总线；写入逻辑1，本来电平也是高电平，故这里无影响 */
	DS18B20_DQ_OUT(HIGH);

	/* 保持采样间隔 */
	w1_delay(12);

}

/**
 * @name: static void DS18B20_writeByte(uint8_t Bety)
 * @description: ds18b20 写一个字节
 * @param {uint8_t} Bety 写入的字节
 * @return {*}
 */
static void DS18B20_writeByte(uint8_t Bety)
{
	uint8_t		i = 0;

	for(; i<8; i++)
	{
		/* 这里是先将8位的Byte先右移i位，再进行位与运算，位与运算遇1为本身，遇0为0，保证了每次循环写Byte的第i位 */
		DS18B20_writeBit((Bety >> i)&0x01);
	}

	printk("%s():%d DS18B20 writeByte %x \n", __FUNCTION__, __LINE__, Bety);

}


/**
 * @name: static uint16_t DS18B20_SampleData(struct gpio_w1_priv *priv)
 * @description: 读取温度值
 * @param {gpio_w1_priv} *priv 私有数据结构体
 * @return {*}
 */
static uint16_t DS18B20_SampleData(struct gpio_w1_priv *priv)
{
	unsigned long	flags;
	uint16_t		temp = 0;
	uint8_t			temp_H = 0;
	uint8_t			temp_L = 0;
	int				rv = 0;

	spin_lock_irqsave(&priv->lock, flags);	// 获取自旋锁

	/* 1. 主机发送启动信号,且从设备回应 */
	rv = DS18B20_start();
	if(rv != 0)
	{
		printk("%s():%d DS18B20_start failed\n", __FUNCTION__, __LINE__);
		rv = -EFAULT;
		goto undo_spin_unlock;
	}

	DS18B20_writeByte(Skip_ROM);	// 跳过ROM，直接对总线上的所有设备进行操作
	DS18B20_writeByte(Convert_T);	// 启动温度转换

	spin_unlock_irqrestore(&priv->lock, flags);	// 释放自旋锁

	/* 2. 等待转换完成 */
	msleep(750);

	spin_lock_irqsave(&priv->lock, flags);	// 重新获取自旋锁

	/* 3. 主机发送启动信号,且从设备回应 */
	rv = DS18B20_start();
	if(rv != 0)
	{
		printk("%s():%d DS18B20_start failed\n", __FUNCTION__, __LINE__);
		rv = -EFAULT;
		goto undo_spin_unlock;
	}

	DS18B20_writeByte(Skip_ROM);	// 跳过ROM，直接对总线上的所有设备进行操作
	DS18B20_writeByte(Read_Data);	// 读取温度数据

	/* 4. 读取温度数据 */
	temp_L = DS18B20_readByte();	// 先读低8位
	temp_H = DS18B20_readByte();	// 再读高8位

	spin_unlock_irqrestore(&priv->lock, flags);	// 释放自旋锁


	temp = (temp_H << 8) | temp_L;	// 将高8位和低8位合并成一个16位的数据

	/* 5. 如果温度为负数，需要进行补码操作 */
	if(temp & 0x8000)	// 判断最高位是否为1，其实也可以直接判断temp是否<0
	{
		temp = ~temp + 1;
	}

	return temp;

undo_spin_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);	// 释放自旋锁
	return rv;

}

/* 字符设备操作函数集 */

/**
 * @name: static int w1_open(struct inode *inode, struct file *file)
 * @description: 打开设备
 * @param {inode} *inode 传递给驱动的inode
 * @param {file} *file 设备文件，file结构体有个叫做private_data的成员变量,一般在open的时候将private_data指向设备结构体。
 * @return 0 successfully , !0 failure
 */
static int w1_open(struct inode *inode, struct file *file)
{
	struct gpio_w1_priv *priv;

	printk("%s():%d w1_open platform driver\n", __FUNCTION__, __LINE__);

	priv = container_of(inode->i_cdev, struct gpio_w1_priv, cdev);	// 找到对应的私有数据结构体的地址
	file->private_data = priv;	// 将私有数据结构体的地址赋值给file->private_data

	return 0;
}

/**
 * @name: static ssize_t w1_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
 * @description: 从设备中读取数据
 * @param {file} *filp	设备文件，文件描述符
 * @param {char __user} *buf	返回给用户空间的数据缓存区
 * @param {size_t} cnt	要读取的数据长度
 * @param {loff_t} *off	相对于文件首地址的偏移量
 * @return {*} 读取的字节数， 负数表示读取失败
 */
static ssize_t w1_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
	int rv = 0;
	uint16_t temp = 0;

	struct gpio_w1_priv *priv = filp->private_data;	// 获取私有数据结构体的地址

	temp = DS18B20_SampleData(priv);	// 读取温度值
	printk("%s():%d DS18B20_SampleData temp = %x\n", __FUNCTION__, __LINE__, temp);

	rv = copy_to_user(buf, &temp, sizeof(temp));	// 将温度值拷贝到用户空间
	if(rv)
	{
		dev_err(priv->dev, "copy_to_user failed\n");
		return -EFAULT;
	}
	else
	{
		rv = sizeof(temp);
	}

	return rv;
}

/**
 * @name: static int w1_release(struct inode *inode,struct file *filp)
 * @description: 关闭设备
 * @param {inode} *inode 传递给驱动的inode
 * @param {file} *filp 设备文件，file结构体有个叫做private_data的成员变量,一般在open的时候将private_data指向设备结构体。
 * @return {*} 0 successfully , !0 failure
 */
static int w1_release(struct inode *inode,struct file *filp)
{
	printk("%s():%d w1_release\n", __FUNCTION__, __LINE__);
	return 0;
}

/* 设备操作函数集 */
static struct file_operations w1_fops = {
	.owner = THIS_MODULE,
	.open = w1_open,
	.read = w1_read,
	.release = w1_release,
};

/**
 * @name: static ssize_t temp_show(struct device *devp, struct device_attribute *attr, char *buf)
 * @description:  温度属性显示函数
 * @param {device} *devp 设备指针,创建file时候会指定dev
 * @param {device_attribute} *attr 设备属性,创建时候传入
 * @param {char} *buf 传出给sysfs中显示的buf
 * @return 显示的字节数
 */
static ssize_t temp_show(struct device *devp, struct device_attribute *attr, char *buf)
{
	uint16_t temp = 0;
	struct gpio_w1_priv *priv = dev_get_drvdata(devp);	// 获取私有数据结构体的地址

	temp = DS18B20_SampleData(priv);	// 读取温度值

	return sprintf(buf, "temp = %d\n", temp*625); // 温度值 = temp * 0.0625, 这里返回的是温度值的10000倍
}

/**
 * @name: static ssize_t temp_store(struct device *devp, struct device_attribute *attr, const char *buf, size_t count)
 * @description: echo写入属性函数
 * @param {device} *devp 设备指针,创建file时候会指定dev
 * @param {device_attribute} *attr 设备属性,创建时候传入
 * @param {char} *buf 用户空间的buf
 * @param {size_t} count 传入buf的size
 * @return 写入的buf大小
 */
static ssize_t temp_store(struct device *devp, struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

/* 声明并初始化一个device_attribute结构体 */
DEVICE_ATTR(temp, 0644, temp_show, temp_store);

/**
 * @name: static int gpio_w1_probe(struct platform_device *pdev)
 * @description: probe函数实现字符设备的注册和设备树的解析
 * @param {platform_device} *pdev 平台设备指针
 * @return {*}	0 successfully , !0 failure
 */
static int gpio_w1_probe(struct platform_device *pdev)
{
	struct gpio_w1_priv 	*priv;	// 私有数据结构体
	struct device			*dev;	// 设备指针
	dev_t					devno;	// 设备号
	int						rv;		// 返回值

	/* 解析设备树并初始化w1状态 */
	rv = parser_dt_init_w1(pdev);
	if(rv < 0)
	{
		dev_err(&pdev->dev, "parser_dt_init_w1 failed\n");
		return rv;
	}

	/* 将之前存入的私有属性放入临时结构体中 */
	priv = platform_get_drvdata(pdev);

	/*---------------------注册 字符设备部分-----------------*/
	/* 1.分配设备号 */
	if(dev_major != 0)
	{
		// 静态分配
		devno = MKDEV(dev_major, 0);
		rv = register_chrdev_region(devno, 1, DEV_NAME);	// /proc/devices/my_w1
	}
	else
	{
		// 动态分配
		rv = alloc_chrdev_region(&devno, 0, 1, DEV_NAME);	// 动态申请字符设备号
		dev_major = MAJOR(devno);	// 获取主设备号
	}

	/* 分配设备号失败 */
	if(rv < 0)
	{
		printk("%s driver can't get major %d\n", DEV_NAME, dev_major);
		return rv;
	}
	printk("%s driver get major %d\n", DEV_NAME, dev_major);

	/* 2.分配cdev结构体，绑定主次设备号、file_operations结构体，并注册给linux内核 */
	priv->cdev.owner = THIS_MODULE;
	cdev_init(&priv->cdev, &w1_fops);	// 初始化cdev结构体
	
	rv = cdev_add(&priv->cdev, devno, DEV_CNT);	/*注册给内核,设备数量1个*/
	if(rv < 0)
	{
		printk("%s driver add cdev failed\n", DEV_NAME);
		goto undo_major;
	}
	printk("%s driver add cdev success, cdev:result=%d\n", DEV_NAME, rv);

	/* 3.创建类 */
	priv->dev_class = class_create(THIS_MODULE, DEV_NAME); /* /sys/class/my_w1 创建类*/
	if(IS_ERR(priv->dev_class))
	{
		printk("%s driver create class failed\n", DEV_NAME);
		rv = -ENOMEM;
		goto undo_cdev;
	}
	printk("%s driver create class success\n", DEV_NAME);

	/* 4.创建设备 */
	devno = MKDEV(dev_major, 0);	// 获取设备号
	dev = device_create(priv->dev_class, NULL, devno, NULL, DEV_NAME);	// 创建设备
	if(IS_ERR(priv->dev_class))
	{
		printk("%s driver create device failed\n", DEV_NAME);
		rv = -ENOMEM;
		goto undo_class;
	}

	/* 5.初始化自旋锁 */
	spin_lock_init(&priv->lock);

	/* 6.创建sys属性在platform_device中 */
	rv = device_create_file(dev, &dev_attr_temp);
	if(rv)
	{
		rv = -ENOMEM;
		goto undo_device;
	}

	priv->dev = dev;	// 将设备指针存入私有数据结构体中

	/* 7.保存私有数据结构体指针 */
	platform_set_drvdata(pdev, priv);
	dev_set_drvdata(priv->dev, priv);
	dev_info(&pdev->dev, "gpio_w1_probe success\n");

	return 0;

undo_device:
	device_destroy(priv->dev_class, devno);

undo_class:
	class_destroy(priv->dev_class);

undo_cdev:
	cdev_del(&priv->cdev);

undo_major:
	unregister_chrdev_region(devno, 1);

	return rv;
}

/**
 * @name: static int gpio_w1_remove(struct platform_device *pdev)
 * @description: 设备卸载时候执行
 * @param {platform_device} *pdev 平台设备指针
 * @return {*} 0 successfully , !0 failure
 */
static int gpio_w1_remove(struct platform_device *pdev)
{
	struct gpio_w1_priv *priv = platform_get_drvdata(pdev); // 获取私有数据结构体指针

	dev_t devno = MKDEV(dev_major, 0);	// 获取设备号

	/* 删除sys属性 */
	device_remove_file(priv->dev, &dev_attr_temp);

	device_destroy(priv->dev_class, devno);	// 销毁设备

	class_destroy(priv->dev_class);	// 销毁类
	cdev_del(&priv->cdev);	// 销毁cdev结构体
	unregister_chrdev_region(devno, DEV_CNT);	// 释放设备号

	gpiod_set_value(w1_gpiod, 0);	// 关闭gpio
	gpiod_put(w1_gpiod);	// 释放gpio

	devm_kfree(&pdev->dev, priv);	// 释放私有数据结构体

	printk("%s driver remove\n", DEV_NAME);
	return 0;
}


/**
 * @name: static void imx_w1_shutdown(struct platform_device *pdev)
 * @description: 设备停止执行的函数
 * @param {platform_device} *pdev 平台设备指针
 * @return {*}
 */
static void imx_w1_shutdown(struct platform_device *pdev)
{
	struct gpio_w1_priv *priv = platform_get_drvdata(pdev); // 获取私有数据结构体指针

	gpiod_set_value(w1_gpiod, 0);	// 关闭gpio
}

/*------------------第一部分----------------*/
static const struct of_device_id w1_match_table[] = {
	{ .compatible = "ds18b20-gpio" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, w1_match_table);

/* 定义平台驱动结构体 */
static struct platform_driver gpio_w1_driver = {
	.probe			= gpio_w1_probe,	// 驱动安装时候会执行的钩子函数
	.remove			= gpio_w1_remove,	// 驱动卸载时候会执行的钩子函数
	.shutdown		= imx_w1_shutdown,	// 设备停止执行的函数
	.driver			= {
		.name		= "ds18b20-gpio",	// 无设备树时，用于设备和驱动间匹配
		.owner		= THIS_MODULE,
		.of_match_table = w1_match_table,	// 有设备树后，利用设备树匹配表
	},
};

/*------------------第二部分----------------*/
/*驱动初始化函数*/
static int __init platdrv_gpio_w1_init(void)
{
	int		rv = 0;

	rv = platform_driver_register(&gpio_w1_driver);		// 注册platform的w1驱动
	if(rv)
	{
		printk(KERN_ERR "%s:%d: Can't register platform driver %d\n", __FUNCTION__, __LINE__, rv);
		return rv;
	}
	printk("Register w1 Platform Driver Successfully!\n");
	return 0;
}

/*------------------第三部分----------------*/
/*驱动注销函数*/
static void __exit platdrv_gpio_w1_exit(void)
{
	printk("%s():%d remove w1 platform driver\n", __FUNCTION__, __LINE__);
	platform_driver_unregister(&gpio_w1_driver);	// 卸载驱动
}

module_init(platdrv_gpio_w1_init);
module_exit(platdrv_gpio_w1_exit);

MODULE_AUTHOR("NongJieYing");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIO W1 driver on i.MX6ULL platform");
MODULE_ALIAS("platform:w1-dev");