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
	struct time_list			timer;			// 定时器用于消抖
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

	value = atomic_read(&priv->keyvalue);			// 保存按键值
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

}






