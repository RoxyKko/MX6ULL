/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  key_irq.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月27日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月27日 06时32分28秒"
 *                 
 ********************************************************************************/


#include <linux/init.h>						// 驱动程序必须的头文件
#include <linux/module.h>					// 驱动程序必须的头文件
#include <linux/errno.h>					// ENODEV，ENOMEM存放的头文件
#include <linux/types.h>					// u8，u32，dev_t等类型在该头文件中定义
#include <linux/kernel.h>					// printk()，内核打印函数
#include <linux/fs.h>						// file_operations，用于联系系统调用和驱动程序
#include <linux/cdev.h>						// cdev_alloc()，分配cdev结构体
#include <linux/device.h>					// 用于自动生成设备节点的函数头文件
#include <linux/of_gpio.h>					// gpio子系统的api
#include <linux/platform_device.h>			// platform总线驱动头文件
#include <linux/gpio.h>						
#include <linux/of_device.h>				
#include <linux/uaccess.h>					// 内核和用户传输数据的函数
#include <linux/of_irq.h>					// 中断相关函数
#include <linux/irq.h>						// 中断相关函数
#include <linux/interrupt.h>
#include <linux/timer.h>					// 定时器相关函数


#define KEY_NAME				"key_irq"
#define KEY0VALUE				0xF0		// 按键值
#define INVAKEY					0x00		// 无效的按键值

static int						dev_major = 0;


// 存放key信息结构体
struct platform_key_data
{
	char						name[16];	// 设备名字
	int							key_gpio;	// gpio编号
	unsigned char				value;		// 按键值
	
	int							irq;		// 中断号
	irqreturn_t (*handler)(int, void*);		// 中断处理函数
};


// 存放key的私有属性
struct platform_key_priv
{
	struct cdev					cdev;			// cdev结构体
	struct class				*dev_class;		// 自动创建设备节点的类
	int							num_key;		// key的数量
	struct platform_key_data	key;			// 存放key信息的结构体数组

	atomic_t					keyvalue;		// 有效的按键键值，用于向应用层上报，原子变量
	atomic_t					releasekey;		// 标记是否完成一次完成的按键，用于向应用层上报
	struct timer_list			timer;			// 定时器用于消抖
};

// 为key私有属性开辟存储空间的函数
static inline int sizeof_platform_key_priv(int num_key)
{
	return sizeof(struct platform_key_priv) + (sizeof(struct platform_key_data) * num_key);
}

// 中断服务函数，初始化定时器用于消抖
static irqreturn_t key0_handler(int irq, void *dev_id)
{
	struct platform_key_priv *priv = (struct platform_key_priv *)dev_id;

	// 开启定时器
	// priv->timer.data = (volatile long)dev_id;
	mod_timer(&(priv->timer), jiffies + msecs_to_jiffies(10));

	return IRQ_RETVAL(IRQ_HANDLED);
}

// 定时器服务函数，定时器到了后的操作
// 定时器到了以后再次读取按键，如果按键还是按下状态则有效
void timer_function(struct timer_list *t)
{
	unsigned char value;
	struct platform_key_priv *priv = from_timer(priv, t, timer);

	// 读取按下的io值
	value = gpio_get_value(priv->key.key_gpio);
	if(value == 0) // 按键按下
	{
		atomic_set(&(priv->keyvalue), value);
		printk("keypress\n");
	}
	else // 按键松开
	{
		atomic_set(&(priv->keyvalue), 0x80 | value);
		atomic_set(&(priv->releasekey), 1);	// 标记松开按键，即完成一次完整的按键过程
		printk("keyrelease\n");
	}
}

// 解析设备树，初始化key属性并初始化中断
int parser_dt_init_key(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;		// 当前设备节点
	struct platform_key_priv *priv;					// 存放私有属性
	int num_key, gpio;								// key数量和gpio编号
	int ret;

	/* 1、按键初始化 */
	num_key = 1;
	if(num_key <= 0)
	{
		dev_err(&pdev->dev, "fail to fine node\n");
		return -EINVAL;
	}

	// 分配存储空间用于存储按键的私有数据
	priv = devm_kzalloc(&pdev->dev, sizeof_platform_key_priv(num_key), GFP_KERNEL);
	if(!priv)
	{
		return -ENOMEM;
	}

	// 通过dts属性名称获取gpio编号
	gpio = of_get_named_gpio(np, "gpios", 0);

	// 将子节点的名字，传给私有属性结构体中的key信息结构体中的name属性
	strncpy(priv->key.name, np->name, sizeof(priv->key.name));

	// 将gpio编号和控制亮灭的标志传给结构体
	priv->key.key_gpio = gpio;

	// 申请gpio口，相较于gpio_request增加了gpio资源获取与释放功能
	if( (ret = devm_gpio_request(&pdev->dev, priv->key.key_gpio, priv->key.name)) < 0)
	{
		dev_err(&pdev->dev, "can't request gpio output for %s\n", priv->key.name);
		return ret;
	}

	// 设置gpio为输入模式，并设置初始状态
	if( (ret = gpio_direction_input(priv->key.key_gpio)) < 0)
	{
		dev_err(&pdev->dev, "can't request gpio output for %s\n", priv->key.name);
	}

	/* 2、中断初始化 */
	// 从设备树中获取中断号
	priv->key.irq = irq_of_parse_and_map(np, 0);
	// 申请中断，并初始化value和中断处理函数
	priv->key.handler = key0_handler;
	priv->key.value = KEY0VALUE;

	ret = request_irq(priv->key.irq, priv->key.handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, priv->key.name, priv);
	if(ret < 0)
	{
		printk("fail to request irq %d\n", priv->key.irq);
		return -EFAULT;
	}

	/* 初始化timer，设置定时器处理函数，还未设置周期，故不会激活定时器 */
	timer_setup(&(priv->timer), timer_function, 0);
	priv->timer.expires = jiffies + HZ/5;
	priv->timer.function = timer_function;
	add_timer(&priv->timer);

	// 暂时先解决一个按键的问题
	priv->num_key = 1;
	dev_info(&pdev->dev, "success to get %d valid key\n", priv->num_key);

	// 将key的私有属性放入platform_device结构体的device结构体中的私有数据中
	platform_set_drvdata(pdev, priv);

	return 0;
}


// 按键打开
static int key_open(struct inode *inode, struct file *file)
{
	struct platform_key_priv *priv;

	priv = container_of(inode->i_cdev, struct platform_key_priv, cdev);
	file->private_data = priv;

	return 0;
}

static ssize_t key_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
	unsigned char value;
	int ret = 0;

	struct platform_key_priv *priv;
	priv = filp->private_data;

	if(gpio_get_value(priv->key.key_gpio) == 0)		// key按下
	{
		while(!gpio_get_value(priv->key.key_gpio));	// 等待按键释放
		atomic_set(&(priv->keyvalue), KEY0VALUE);
	}
	else // 无效的按键值
	{
		atomic_set(&(priv->keyvalue), INVAKEY);
	}

	value = atomic_read(&(priv->keyvalue));			// 保存按键值
	ret = copy_to_user(buf, &value, sizeof(value));

	return ret;
}

static int key_release(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations key_fops = 
{
	.owner = THIS_MODULE,
	.open = key_open,
	.read = key_read,
	.release = key_release,
};

static int platform_key_probe(struct platform_device *pdev)
{
	struct platform_key_priv	*priv;	// 临时存放私有属性结构体
	struct device				*dev;	// 设备结构体
	dev_t						devno;	// 设备的主次设备号
	int							i, rv=0;

	// 1、解析设备树并初始化key状态
	rv = parser_dt_init_key(pdev);
	if(rv<0)
	{
		return rv;
	}

	// 将之前存入的私有属性放入临时存放的结构体中
	priv = platform_get_drvdata(pdev);
	
	// 2、分配主次设备号
	if(dev_major != 0)
	{
		// 静态分配设备号
		devno = MKDEV(dev_major, 0);
		rv = register_chrdev_region(devno, priv->num_key, KEY_NAME);	// /proc/devices/key_irq
	}
	else
	{
		// 动态分配主次设备号
		rv = alloc_chrdev_region(&devno, 0, priv->num_key, KEY_NAME);
		dev_major = MAJOR(devno);
	}

	if(rv < 0)
	{
		dev_err(&pdev->dev, "major can't be allocated\n");
		return rv;
	}

	// 3、分配cdev结构体
	cdev_init(&priv->cdev,  &key_fops);
	priv->cdev.owner = THIS_MODULE;

	rv = cdev_add(&priv->cdev, devno, priv->num_key);
	if(rv < 0)
	{
		dev_err(&pdev->dev, "struture cdev can't be allocated\n");
		goto undo_major;
	}

	// 4、创建类，实现自动创建设备节点
	priv->dev_class = class_create(THIS_MODULE, "key"); // /sys/class/key
	if( IS_ERR(priv->dev_class) )
	{
		dev_err(&pdev->dev, "fail to create class\n");
		rv = -ENOMEM;
		goto undo_cdev;
	}

	// 5、创建设备
	for(i = 0; i<priv->num_key; i++)
	{
		devno = MKDEV(dev_major, i);
		dev = device_create(priv->dev_class, NULL, devno, NULL, "key%d", i); // /dev/key0
		if( IS_ERR(dev) )
		{
			dev_err(&pdev->dev, "fail to create device\n");
			rv = -ENOMEM;
			goto undo_class;
		}
	}

	printk("success to install driver[major=%d]!\n", dev_major);

	return 0;
undo_class:
	class_destroy(priv->dev_class);

undo_cdev:
	cdev_del(&priv->cdev);

undo_major:
	unregister_chrdev_region(devno, priv->num_key);

	return rv;
}

static int platform_key_remove(struct platform_device *pdev)
{
	struct platform_key_priv *priv = platform_get_drvdata(pdev);
	int i;
	dev_t devno = MKDEV(dev_major, 0);

	// 注销设备结构体，class结构体和cdev结构体
	for(i = 0; i < priv->num_key; i++)
	{
		devno = MKDEV(dev_major, i);
		device_destroy(priv->dev_class, devno);
	}
	class_destroy(priv->dev_class);

	cdev_del(&priv->cdev);
	unregister_chrdev_region(MKDEV(dev_major, 0), priv->num_key);

	// 将key的状态设置为0
	for(i = 0; i< priv->num_key; i++)
	{
		gpio_set_value(priv->key.key_gpio, 0);
	}

	// 删除定时器
	del_timer_sync(&(priv->timer));

	// 释放中断
	free_irq(priv->key.irq, priv);

	printk("success to remove driver[major=%d]!\n",dev_major);
	return 0;
}

// 匹配列表
static const struct of_device_id platform_key_of_match[] = {
	{ .compatible = "my-gpio-keys" },
	{}
};

MODULE_DEVICE_TABLE(of, platform_key_of_match);

// platform驱动结构体
static struct platform_driver platform_key_driver = {
	.driver		= {
		.name	=	"key_irq",						// 无设备树时，用于设备和驱动间的匹配
		.of_match_table = platform_key_of_match,	// 有设备树后，利用设备树匹配表
	},

	.probe		=	platform_key_probe,
	.remove		=	platform_key_remove,
};

module_platform_driver(platform_key_driver);

MODULE_AUTHOR("NongJieYing");
MODULE_LICENSE("GPL");




