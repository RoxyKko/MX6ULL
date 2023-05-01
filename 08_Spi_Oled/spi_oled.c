/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  spi_oled.c
 *    Description:  This file spi oled driver
 *                 
 *        Version:  1.0.0(2023年05月01日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年05月01日 02时33分05秒"
 *                 
 ********************************************************************************/


#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>                   //file_operations，用于联系系统调用和驱动程序
#include <linux/errno.h>                //ENODEV，ENOMEM存放的头文件
#include <linux/kernel.h>               // printk()，内核打印函数
#include <linux/device.h>               // 用于自动生成设备节点的函数头文件
#include <linux/gpio.h>                 // gpio相关函数
#include <linux/gpio/consumer.h>        // gpiod相关函数
#include <linux/of_gpio.h>              // gpio子系统相关函数
#include <linux/uaccess.h>              // copy_to_user函数头文件
#include <linux/timer.h>                //定时器相关
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include <linux/of_address.h>
#include <asm/mach/map.h>

#define DEV_NAME			"spi_oled"
#define DEV_CNT				1

#ifndef DEV_MAJOR
#define DEV_MAJOR 			0
#endif

#define PLATDRV_MAGIC		0x16		// 魔术字
#define OLED_ON				_IO (PLATDRV_MAGIC, 0x18)

unsigned char				*baseaddr;

static int 					dev_major = DEV_MAJOR;	/* 主设备号 */

int dc_gpio;		// 数据，命令选择线使用的gpio编号
int reset_gpio;		// 复位所使用的gpio编号
int cs_gpio;		// 片选所使用的gpio编号

// OLED命令枚举
enum
{
	OLED_CMD = 0,
	OLED_DATA,
};

/* OLED显示屏行列像素 */
#define X_WIDTH		128
#define Y_WIDTH		64

/* 存放oled的私有属性 */
struct oled_priv {
	struct cdev			cdev;
	struct class		*dev_class;
	struct spi_device	*spi;
	struct device		*dev;
};

/*
* 向 oled 发送数据
* spi_device，指定oled 设备驱动的spi 结构体
* data, 要发送数据的地址
* length，发送的数据长度
*/
static int oled_send(struct spi_device *spi_device, unsigned char *buf, int length)
{
	int error = 0;
	int index = 0;
	struct spi_message *message;	// 定义发送的消息
	struct spi_transfer *transfer;	// 定义传输结构体

	/* 使能oled，拉低片选 */
	gpio_set_value(cs_gpio, 0);

	/* 申请空间 */
	message = kzalloc(sizeof(struct spi_message), GFP_KERNEL);
	transfer = kzalloc(sizeof(struct spi_transfer), GFP_KERNEL);

	/* 每次发送30字节，循环发送 */
	while(length > 0)
	{
		if(length > 30)
		{
			transfer->tx_buf = buf + index;
			transfer->len = 30;
			spi_message_init(message);
			spi_message_add_tail(transfer, message);
			index += 30;
			length -= 30;
		}
		else
		{
			transfer->tx_buf = buf + index;
			transfer->len = length;
			spi_message_init(message);
			spi_message_add_tail(transfer, message);
			index += length;
			length = 0;
		}

		error = spi_sync(spi_device, message);
		if(error != 0)
		{
			printk("spi_sync error! %d \n", error);
			return -1;
		}

	}

	// alloc后一定要free
	kfree(message);
	kfree(transfer);

	return 0;
}

/* 
 * oled写一个字节命令或单字节数据 
 * type: 1发送数据，0发送命令
 * data: 发送的字节
 * *priv: oled的私有数据
 * */
static void oled_send_oneByte(struct oled_priv *priv, unsigned char data, unsigned short type)
{
	if(type)
	{
		gpio_set_value(dc_gpio, OLED_DATA);
	}
	else
	{
		gpio_set_value(dc_gpio, OLED_CMD);
	}

	oled_send(priv->spi, &data, 1);
	gpio_set_value(dc_gpio, OLED_DATA);

}

/* oled 显示函数API */

/**
 * @name: static int oled_set_pos(struct oled_priv *priv, uint8_t x, uint8_t y)
 * @description: 设置oled显示位置
 * @param {oled_priv} *priv
 * @param {uint8_t} x 
 * @param {uint8_t} y
 * @return {*}
 */
static int oled_set_pos(struct oled_priv *priv, uint8_t x, uint8_t y)
{
	oled_send_oneByte(priv, 0xb0+y, OLED_CMD);					// 设置行起始位置
	oled_send_oneByte(priv, ((x&0xf0)>>4)|0x10, OLED_CMD);		// 设置低列起始位置
	oled_send_oneByte(priv, (x&0x0f)|0x01, OLED_CMD);			// 设置高列起始位置

	return 0;
}

/**
 * @name: static void oled_clear(struct oled_priv *priv)
 * @description: OLED显示屏清屏
 * @param {oled_priv} *priv
 * @return {*}
 */
static void oled_clear(struct oled_priv *priv)
{
	uint8_t x,y;

	for(y=0; y<8; y++)
	{
		oled_set_pos(priv, 0, y);
		for(x=0; x<X_WIDTH; x++)
		{
			oled_send_oneByte(priv, 0x00, OLED_DATA);
		}
	}
}

/** static int oled_display_buffer(struct oled_priv *priv, uint8_t *display_buffer, int length)
 * @name: 
 * @description: 向oled发送显示数据， x,y指定显示的起始位置，支持自动换行
 * @param {oled_priv} *priv 指定oled设备驱动的spi结构体
 * @param {uint8_t} *display_buffer 数据地址
 * @param {int} length 发送长度
 * @return 0 成功;其他 失败
 */
static int oled_display_buffer(struct oled_priv *priv, uint8_t *display_buffer, int length)
{
	uint8_t x = 0;
	uint8_t y = 0;
	int index = 0;
	int error = 0;

	while(length > 0)
	{
		error += oled_set_pos(priv, x, y);
		if(length > (X_WIDTH - x))
		{
			error += oled_send(priv->spi, display_buffer + index, X_WIDTH - x);
			length -= (X_WIDTH - x);
			index += (X_WIDTH -x);
			x = 0;
			y++;
		}
		else
		{
			error += oled_send(priv->spi, display_buffer + index, length);
			index += length;
			length = 0;
		}
	}

	if(error != 0)
	{
		/* 发送错误 */
		printk("oled_display_buffer error ! %d\n",error);
		return -1;
	}

	return index;
}

/**
 * @name: static void oled_init(struct oled_priv *priv)
 * @description: oled寄存器初始化
 * @param {oled_priv} *priv
 * @return {*}
 */
static void oled_init(struct oled_priv *priv)
{
	gpio_set_value(reset_gpio, 1);
	mdelay(100);
	gpio_set_value(reset_gpio, 0);
	mdelay(100);
	gpio_set_value(reset_gpio, 1);

	oled_send_oneByte(priv, 0xAE, OLED_CMD);		// 关闭OLED面板
	oled_send_oneByte(priv, 0xFD, OLED_CMD);
    oled_send_oneByte(priv, 0x12, OLED_CMD); 
	oled_send_oneByte(priv, 0xd5, OLED_CMD); 		// 设置显示时钟分频比/振荡器频率
	oled_send_oneByte(priv, 0xa0, OLED_CMD);		// 设置 COM 输出扫描方向
	oled_send_oneByte(priv, 0xa8, OLED_CMD);		// 设置复用比（1 到 64）
	oled_send_oneByte(priv, 0x3f, OLED_CMD);		// 1/64 周期
	oled_send_oneByte(priv, 0xd3, OLED_CMD);		// 设置显示偏移量 Shift Mapping RAM Counter (0x00~0x3F)
	oled_send_oneByte(priv, 0x00, OLED_CMD);		// 不偏移
	oled_send_oneByte(priv, 0x40, OLED_CMD);		// 设置起始行地址 设置映射 RAM 显示起始行 (0x00~0x3F)
	oled_send_oneByte(priv, 0xa1, OLED_CMD);		// 设置 SEG/列映射 0xa0左右反置 0xa1正常
	oled_send_oneByte(priv, 0xc8, OLED_CMD);		// 设置 COM/行映射 0xc0上下反置 0xc8正常
	oled_send_oneByte(priv, 0xda, OLED_CMD);		// 设置 com 引脚硬件配置
	oled_send_oneByte(priv, 0x12, OLED_CMD);		// 
	oled_send_oneByte(priv, 0x81, OLED_CMD);		// 设置对比度控制寄存器
	oled_send_oneByte(priv, 0xbf, OLED_CMD);		// 设置 SEG 输出电流亮度
	oled_send_oneByte(priv, 0xd9, OLED_CMD);		// 设置预充电期
	oled_send_oneByte(priv, 0x25, OLED_CMD);		// 将预充电设置为 15 个时钟，将放电设置为 1 个时钟
	oled_send_oneByte(priv, 0xdb, OLED_CMD);		// 设置 vcomh
	oled_send_oneByte(priv, 0x34, OLED_CMD);		// 设置 VCOM 取消选择电平
	oled_send_oneByte(priv, 0xa4, OLED_CMD);		// 禁用整个显示 (0xa4/0xa5)
	oled_send_oneByte(priv, 0xa6, OLED_CMD);		// 禁用反向显示 (0xa6/a7)
	oled_send_oneByte(priv, 0xaf, OLED_CMD);		// 
	oled_clear(priv);

}

/**
 * @name: static int oled_open(struct inode *inode, struct file *filp)
 * @description: 字符设备操作函数，open函数实现
 * @param {inode} *inode 设备节点
 * @param {file} *filp 
 * @return {*}
 */
static int oled_open(struct inode *inode, struct file *filp)
{
	struct oled_priv *priv = NULL;

	printk("oled_open \n");

	priv = container_of(inode->i_cdev, struct oled_priv, cdev);

	filp->private_data = priv;
	oled_init(priv);
	printk("oled_init \n");

	return 0;
}

/**
 * @name: static ssize_t oled_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
 * @description: 字符设备操作函数，.read函数实现
 * @param {file} *filp	设备文件，文件描述符
 * @param {char __user} *buf	返回给用户空间的数据缓存区
 * @param {size_t} cnt	要读取的数据长度
 * @param {loff_t} *off	相对于文件首地址的偏移量
 * @return {*} 读取的字节数， 负数表示读取失败
 */
static ssize_t oled_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
	int ret = 0;
	ret = copy_to_user(buf, baseaddr, 1024);

	return ret;
}

/**
 * @name: static int oled_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *off)
 * @description: 字符设备操作函数，.write函数实现
 * @param {file} *filp	设备文件，文件描述符
 * @param {char __user} *buf	返回给用户空间的数据缓存区
 * @param {size_t} cnt	要读取的数据长度
 * @param {loff_t} *off	相对于文件首地址的偏移量
 * @return {*} 0 成功;其他 失败
 */
static int oled_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *off)
{
    struct  oled_priv *priv = filp->private_data;
    unsigned char		databuf[1024];
    int     rv = 0;

    /*数据发送*/
    rv = copy_from_user(databuf, buf, cnt);
    if(rv < 0)
    {
        printk("oled_write fail\r\n");
        return -EFAULT;
    }
    printk("copy_from_user ok:cnt = %d\r\n", cnt);
    
    oled_display_buffer(priv, databuf, cnt);  

    oled_clear(priv);
 
    return 0;
}

/**
 * @name: static int oled_mmap(struct file *filp, struct vm_area_struct *vma)
 * @description: 该函数将OLED显示设备的物理内存映射到用户空间的虚拟内存中。
 * 				 函数接受一个文件指针和一个vm_area_struct指针作为输入参数。
 *				 通过使用pgprot_cached函数设置已映射页面的页保护标志。
 * @param {file} *filp 表示打开的设备文件的file结构体指针
 * @param {struct vm_area_struct} *vma : 描述要映射的虚拟内存范围的虚拟内存区域结构体指针。
 * @return {*} 成功返回0，如果baseaddr为null则返回-1，如果remap_pfn_range函数调用失败则返回-ENOBUFS。
 */
static int oled_mmap(struct file *filp, struct vm_area_struct *vma)
{
	vma->vm_page_prot = pgprot_cached(vma->vm_page_prot);

	if(!baseaddr)
	{
		return -1;
	}
	if( remap_pfn_range(vma, vma->vm_start, virt_to_phys(baseaddr) >> PAGE_SHIFT, vma->vm_end - vma->vm_start, vma->vm_page_prot) )
	{
		return -ENOBUFS;
	}

	return 0;
}

/**
 * @name: static long oled_ioctrl(struct file *filp, unsigned int cmd, unsigned long arg)
 * @description: 这个函数实现了OLED显示设备驱动的IO控制操作。它接受一个文件指针、一个命令和一个参数作为参数。
 * 				 该函数从文件指针中检索私有数据以访问设备的内部状态。
 * 				 该函数目前只支持一个命令 - OLED_ON
 * @param {file} *filp 表示打开的设备文件的文件结构体指针
 * @param {unsigned int} cmd 用户应用程序传递给设备驱动程序的命令代码
 * @param {unsigned long} arg 与命令相关联的参数
 * @return {*} 成功返回0，失败返回错误代码。 
 */
static long oled_ioctrl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct oled_priv *priv = filp->private_data;
	unsigned char *display_buffer = baseaddr;
	switch(cmd)
	{
		case OLED_ON:
			oled_display_buffer(priv, display_buffer, 1024);
			break;
	}

	return 0;
}

/**
 * @name: static int oled_release(struct inode *inode, struct file *filp)
 * @description: 关闭设备
 * @param {inode} *inode 传递给驱动的inode
 * @param {file} *filp 设备文件，file结构体有个叫做private_data的成员变量,一般在open的时候将private_data指向设备结构体。
 * @return {*} 0 successfully , !0 failure
 */
static int oled_release(struct inode *inode, struct file *filp)
{
	struct oled_priv *priv = filp->private_data;
	oled_send_oneByte(priv, 0xAE, OLED_CMD);// 关闭显示
	return 0;
}

/*字符设备操作函数集*/
static struct file_operations oled_fops = 
{
	.owner = THIS_MODULE,
	.open = oled_open,
	.read = oled_read,
	.write = oled_write,
	.unlocked_ioctl = oled_ioctrl,
	.mmap = oled_mmap,
	.release = oled_release,
};

/*spi总线设备函数集:.probe函数只需要初始化esdpi,添加、注册一个字符设备即可。 */
static int oled_probe(struct spi_device *spi_dev)
{
	struct oled_priv	*priv = NULL; //临时存放私有属性的结构体
	dev_t				devno;
	int					rv = 0;
	struct device_node	*np = NULL;

	printk("match successed \n");
	/* 为oled私有属性分配存储空间 */

	priv = devm_kzalloc(&spi_dev->dev, sizeof(struct oled_priv), GFP_KERNEL);
	if(!priv)
	{
		return -EINVAL;
	}

	/* 获取oled的设备树节点 */
	np = of_find_node_by_path("/soc/bus@2000000/spba-bus@2000000/spi@2008000");		// 当前设备节点
	if(np == NULL)
	{
		printk("get ecspi_oled_node failure!\n");
	}

	/* 获取oled的cs引脚并设置为输出，默认为高电平 */
	cs_gpio = of_get_named_gpio(np, "cs-gpio", 0);		// 请求gpio，有请求一定要释放，否则下一次安装将请求失败
	if(cs_gpio < 0)
	{
		printk("can't get cs-gpio\n");
		rv = -EINVAL;
		return rv;
	}
	rv = gpio_request(cs_gpio, "cs-gpio");
	rv = gpio_direction_output(cs_gpio, 1);
	if(rv < 0)
	{
		printk("can't set cs-gpio\n");
	}

	/*获取 oled的reset引脚并设置为输出，默认高电平*/
	reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);	// 请求gpio，有请求一定要释放，否则下一次安装将请求失败
	if(reset_gpio < 0)
	{
		printk("can't get reset-gpio\n");
        return -EINVAL;
	}
	rv = gpio_request(reset_gpio, "reset-gpio");
	rv = gpio_direction_output(reset_gpio, 1);
	if(rv < 0)
	{
		printk("can't set reset-gpio");
	}

	/*获取 oled的dc引脚并设置为输出，默认高电平*/
	dc_gpio = of_get_named_gpio(np, "dc-gpio", 0);			// 请求gpio，有请求一定要释放，否则下一次安装将请求失败
	if(dc_gpio < 0)
	{
		printk("can't get dc-gpio\n");
        return -EINVAL;
	}
	rv = gpio_request(dc_gpio, "dc-gpio");
    rv = gpio_direction_output(dc_gpio, 1);
    if(rv < 0)
    {
        printk("can't set dc-gpio!\r\n");
    }

	 printk("cs_gpio=%d, reset_gpio=%d, dc_gpio=%d\n", cs_gpio, reset_gpio, dc_gpio);

	/*---------------------注册 字符设备部分-----------------*/
    /*1.分配主次设备号，这里即支持静态指定，也至此动态申请*/
	if( dev_major != 0)
	{
		// 静态
		devno = MKDEV(dev_major, 0);
		rv = register_chrdev_region(devno, DEV_CNT, DEV_NAME);	// /proc/devices/DEV_NAME
	}
	else
	{
		// 动态
		rv = alloc_chrdev_region(&devno, 0, DEV_CNT, DEV_NAME);
		dev_major = MAJOR(devno);
	}

	// 分配设备号失败
	if(rv < 0)
	{
		printk("%s driver can't get major %d\n", DEV_NAME, dev_major);
		return rv;
	}
	printk("%s driver use major %d\n", DEV_NAME, dev_major);

	/*2.分配cdev结构体，绑定主次设备号、fops到cdev结构体中，并注册给Linux内核*/
	priv->cdev.owner = THIS_MODULE;		/*.owner这表示谁拥有你这个驱动程序*/
	cdev_init(&priv->cdev, &oled_fops);	/*初始化cdev,把fops添加进去*/
	rv = cdev_add(&priv->cdev, devno, DEV_CNT);	/*注册给内核,设备数量1个*/

	if(rv != 0)
	{
		printk( " %s driver can't register cdev:result=%d\n", DEV_NAME, rv);
		goto undo_major;
	}
	printk( " %s driver can register cdev:result=%d\n", DEV_NAME, rv);

	//3.创建类，驱动中进行节点创建
	priv->dev_class = class_create(THIS_MODULE, DEV_NAME);
	if(IS_ERR(priv->dev_class))
	{
		printk("%s driver create class failure\n", DEV_NAME);
		rv = -ENOMEM;
		goto undo_cdev;
	}

	//4.创建设备
	devno = MKDEV(dev_major, 0);
	priv->dev = device_create(priv->dev_class, NULL, devno, NULL, DEV_NAME);	// /dev/DEV_NAME注册这个设备节点
	if(IS_ERR(priv->dev))
	{
		dev_err(&spi_dev->dev, "fail to create device\n");
		rv = -ENOMEM;
		goto undo_class;
	}

	//5.初始化spi
	spi_dev->mode = SPI_MODE_0;			// spi设置模式为SPI_MODE_0模式
	spi_dev->max_speed_hz = 2000000; 	// 设置最高频率，会覆盖设备树中的设置
	spi_setup(spi_dev);					// 设置spi
	priv->spi = spi_dev;				//传回spi_device结构体，该结构体一个spi设备

	//6. 保存私有数据
	spi_set_drvdata(spi_dev, priv);

	dev_info(&spi_dev->dev, "oled spi driver probe okay.\n");

	return 0;

undo_class:
	class_destroy(priv->dev_class);

undo_cdev:
	cdev_del(&priv->cdev);

undo_major:
	unregister_chrdev_region(devno, DEV_CNT);

	return rv;

}

static int oled_remove(struct spi_device *spi_dev)
{
	/* 删除设备 */
	struct oled_priv *priv = spi_get_drvdata(spi_dev);	// 临时存放私有属性的结构体

	dev_t devno = MKDEV(dev_major, 0);

	devno = MKDEV(dev_major, 0);

	gpio_free(cs_gpio);
	gpio_free(reset_gpio);
	gpio_free(dc_gpio);

	device_destroy(priv->dev_class, devno); /*注销每一个设备号*/

	class_destroy(priv->dev_class);	//注销类
	cdev_del(&priv->cdev);
	unregister_chrdev_region(devno, DEV_CNT);

	devm_kfree(&spi_dev->dev, priv);

	kfree(baseaddr);
	printk("oled driver remove.\n");
	return 0;
}

/*定义设备树匹配表*/
static const struct of_device_id oled_of_match_table[] = {
	{.compatible = "oled_spi"},
	{/* sentinel */}
};

/* 定义spi总线设备结构体 */
struct spi_driver oled_driver = {
	.probe = oled_probe,
	.remove = oled_remove,
	.driver = {
		.name	=	"oled_spi",
		.owner	=	THIS_MODULE,
		.of_match_table = oled_of_match_table,
	},
};

/*
 * 驱动初始化函数
 */
static int __init oled_driver_init(void)
{
	int ret;
	printk("oled_driver_init\n");
	baseaddr = kmalloc(1024, GFP_KERNEL);
	ret = spi_register_driver(&oled_driver);
	return ret;
}

/*
 * 驱动注销函数
 */
static void __exit oled_driver_exit(void)
{
	printk("oled_driver_exit\n");
	spi_unregister_driver(&oled_driver);
}

module_init(oled_driver_init);
module_exit(oled_driver_exit);

MODULE_AUTHOR("NongJieYing");
MODULE_DESCRIPTION("spi_oled driver on i.MX6ULL platform");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:oled");
