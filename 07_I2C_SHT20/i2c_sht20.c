/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  i2c_sht20.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月30日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月30日 01时04分10秒"
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
#include <linux/i2c.h>                  //i2c相关
#include <linux/delay.h>                // 延时函数头文件
#include <linux/cdev.h>                 //cdev相关函数

#define DEV_NAME			"sht20"		// 最后在/dev路径下的设备名称，应用层open的字符串名
#define DEV_CNT				1

#ifndef DEV_MAJOR
#define DEV_MAJOR 0
#endif

/* SHT20命令 */
#define SOFE_RESET					0xfe	// 软复位
#define T_MEASURE_NO_HOLD_CMD		0xf3	// 无主机模式触发温度测量
#define RH_MEASURE_NO_HOLD_CMD		0xf5	// 无主机模式触发湿度测量
#define T_MEASURE_HOLD_CMD			0xe3	// 主机模式触发温度测量
#define RH_MEASURE_HOLD_CMD			0xe5	// 主机模式触发湿度测量

#define CRC_MODEL					0x31
#define CRC_SUCCESS					0
#define CRC_FAIL					1

static int dev_major = DEV_MAJOR;			// 主设备号

/* 存放sht20的私有属性 */
struct sht20_priv {
	struct cdev			cdev;		// 字符设备结构体
	struct class		*dev_class;		// 自动创建设备节点的类
	struct i2c_client	*client;	// i2c设备的client结构体
	struct device		*dev;		// 设备结构体
};

/* sht20软复位 */
static int sht20_soft_reset(struct i2c_client *client)
{
	int				rv = 0;
	struct i2c_msg	sht20_msg;
	uint8_t			cmd_data = SOFE_RESET;

	/* 读取位置12c_msg,发送i2c要写入的地址reg */
	sht20_msg.addr = client->addr;	// sht20在i2c总线上的地址
	sht20_msg.flags = 0;			// flags为0，写入模式
	sht20_msg.buf = &cmd_data;		// 写入的首地址
	sht20_msg.len = 1;				// 写入长度

	/* 复位sht20 */
	rv = i2c_transfer(client->adapter, &sht20_msg, 1);

	// 软复位所需时间不超过15ms
	msleep(15);

	return rv;
}

/* 检查CRC */
static int sht20_crc8(unsigned char *data, int len, unsigned char checksum)
{
	unsigned char 	crc = 0x00;
	int				i,j;

	for(i=0;i<len;i++)
	{
		// crc值对data每个位进行按位异或运算，位相同则为0，否则为1
		crc ^= *data++;

		for(j=0; j<8; j++)
		{
			crc = (crc & 0x80)?(crc << 1) ^ CRC_MODEL : (crc << 1);
		}
	}

	if(checksum == crc)
	{
		return CRC_SUCCESS;
	}
	else
	{
		return CRC_FAIL;
	}
}

/* 通过i2c 读取sht20的数据 */
static int i2c_read_sht20(struct i2c_client *client, uint8_t cmd, void *recv, uint32_t length)
{
	int rv = 0;
	uint8_t cmd_data = cmd;
	struct i2c_msg sht20_msg[2];

	/* 设置读取位置i2c_msg,发送iic要写入的地址 */
	sht20_msg[0].addr = client->addr;	// sht20在i2c总线上的地址
	sht20_msg[0].flags = 0;				// 标记为发送数据
	sht20_msg[0].buf = &cmd_data;		// 写入的首地址
	sht20_msg[0].len = 1;				// 写入长度

	/* 读取i2c_msg */
	sht20_msg[1].addr = client->addr;	// sht20在i2c总线上的地址
	sht20_msg[1].flags = I2C_M_RD;		// 标记为读取数据
	sht20_msg[1].buf = recv;			// 读取得到的数据保存位置
	sht20_msg[1].len = length;			// 读取长度

	rv = i2c_transfer(client->adapter, sht20_msg, 2);

	if(rv != 2)
	{
		printk(KERN_DEBUG "\n i2c_read_sht20 error \n");
		return -1;
	}
	return 0;
}

/* 通过i2c 读取sht20的温湿度数据 */
static int read_t_rh_data(struct i2c_client *client, unsigned char *tx_data)
{
	int rv = 0;

	unsigned char rx_data[6];

	unsigned char checksum[2];
	unsigned char crc_data_t[2];
	unsigned char crc_data_rh[2];

	unsigned char cmd_t = T_MEASURE_HOLD_CMD;
	unsigned char cmd_rh = RH_MEASURE_HOLD_CMD;

	// 形参判断
	if( !client || !tx_data)
	{
		printk("%s line [%d] %s() get invalid input argument\n", __FILE__, __LINE__, __func__);
		return -1;
	}

	// 读取温度数据
	rv = i2c_read_sht20(client, cmd_t, rx_data, 3);
	if(rv < 0)
	{
		dev_err(&client->dev, "i2c recv temp data failure!\n");
		return -1;
	}

	// 读取湿度数据
	rv = i2c_read_sht20(client, cmd_rh, rx_data+3, 3);
	if(rv < 0)
	{
		dev_err(&client->dev, "i2c recv humi data failure!\n");
		return -1;
	}

	// 数据处理，12 bit位的湿度数据和14bit位的温度数据。t+crc+rh+CRC_MODEL
	tx_data[0] = rx_data[0];
	tx_data[1] = ( rx_data[1] & 0xFC ); // 1111 1100
	tx_data[2] = rx_data[3];
	tx_data[3] = ( rx_data[4] & 0xF0 ); // 1111 0000

	// 可以加上CRC校检位
	checksum[0] = rx_data[2];
	checksum[1] = rx_data[5];

	crc_data_t[0] = rx_data[0];
	crc_data_t[1] = rx_data[1];

	crc_data_rh[0] = rx_data[3];
	crc_data_rh[1] = rx_data[4];

	if(sht20_crc8(crc_data_t, 2, checksum[0]) != 0)
	{
		dev_err(&client->dev, "temprature data fails to pass cyclic redundancy check\n");
		return -1;
	}

	if(sht20_crc8(crc_data_rh, 2, checksum[1]) != 0)
	{
		dev_err(&client->dev, "humidity data fails to pass cyclic redundancy check\n");
		return -1;
	}

	return 0;
}


/**
 * @name: static int sht20_open(struct inode *inode, struct file *filp)
 * @description: 字符设备操作函数集，open函数实现
 * @param {inode} *inode 传递给驱动的 inode
 * @param {file} *filp 设备文件，file 结构体有个叫做 private_data 的成员变量一般在 open 的时候将 private_data 指向设备结构体。
 * @return 0 成功;其他 失败
 */
static int sht20_open(struct inode *inode, struct file *filp)
{
	struct sht20_priv *priv;
	int rv;

	printk(" \n sht20_open \n");

	priv = container_of(inode->i_cdev, struct sht20_priv, cdev);

	filp->private_data = priv;

	/* 向sht20发送配置数据，让sht20处于正常工作状态 */
	rv = sht20_soft_reset(priv->client);
	if(rv < 0)
	{
		dev_err(priv->dev, "sht20 init failure\n");
		return -1;
	}

	return 0;
}

/**
 * @name: static ssize_t sht20_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
 * @description: 从设备中读取数据
 * @param {file} *filp	设备文件，文件描述符
 * @param {char __user} *buf	返回给用户空间的数据缓存区
 * @param {size_t} cnt	要读取的数据长度
 * @param {loff_t} *off	相对于文件首地址的偏移量
 * @return {*} 读取的字节数， 负数表示读取失败
 */
static ssize_t sht20_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
    int rv =0;
    struct sht20_priv *priv = filp->private_data;
    unsigned char tx_data[4];

    read_t_rh_data(priv->client, tx_data);

    printk("\n test %x %x %x %x \n", tx_data[0], tx_data[1], tx_data[2], tx_data[3]);

    rv = copy_to_user(buf, tx_data, sizeof(tx_data));
    if(rv)
    {
        dev_err(priv->dev, "copy to user failure!\n");
        return -EFAULT;
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
static int sht20_release(struct inode *inode, struct file *file)
{
    printk("sht20 release\n");
    return 0;
}

/* 字符设备操作函数集 */
static struct file_operations sht20_fops = 
{
    .owner = THIS_MODULE,
    .open = sht20_open,
    .read = sht20_read,
    .release = sht20_release,
};

/**
 * @name: static ssize_t temp_humi_show(struct device *pdev, struct device_attribute *attr, char *buf)
 * @description: 温度属性显示函数
 * @param {device} *pdev 设备指针,创建file时候会指定dev
 * @param {device_attribute} *attr 设备属性,创建时候传入
 * @param {char} *buf 传出给sysfs中显示的buf
 * @return 显示的字节数
 */
static ssize_t temp_humi_show(struct device *pdev, struct device_attribute *attr, char *buf)
{
    int rv = 0;
    unsigned char tx_data[4];
    struct sht20_priv *priv = dev_get_drvdata(pdev);
    unsigned int temp;
    unsigned int humi;

    rv = read_t_rh_data(priv->client, tx_data);
    if(rv < 0)
    {
        dev_err(priv->dev, "read_t_rh_data to show failure!\n");
        return -EFAULT;
    }

    // 为什么14位的temp和12位的humi放大的是同一个倍数呢？
    // 因为temp的单位是C，humi我们需要是%，即百分数
    temp = ( (tx_data[0] << 8) | tx_data[1] );      // 放大100倍

    humi = ( (tx_data[2] << 8) | tx_data[3] );      // 放大100倍
    printk("show_test %x %x %x %x \n", tx_data[0], tx_data[1], tx_data[2], tx_data[3]);

    return sprintf(buf, "temp=%d,humi=%d\n",temp, humi);//1000倍

}

/**
 * @name: static ssize_t temp_humi_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
 * @description: echo写入属性函数
 * @param {device} *dev 设备指针,创建file时候会指定dev
 * @param {device_attribute} *attr 设备属性,创建时候传入
 * @param {char} *buf 用户空间的buf
 * @param {size_t} count 传入buf的size
 * @return {*} 写入的buf大小
 */
static ssize_t temp_humi_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    return count;
}

/*声明并初始化一个 device_attribute结构体*/
DEVICE_ATTR(temp_humi, 0644, temp_humi_show, temp_humi_store);

/*i2c总线设备函数集:.probe函数只需要添加、注册一个字符设备即可。 */
static int sht20_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct sht20_priv   *priv = NULL;   // 临时存放私有属性结构体
    dev_t               devno;          // 设备的主次设备号
    int                 rv = 0;

    // 0.给priv分配空间
    priv = devm_kzalloc(&client->dev, sizeof(struct sht20_priv), GFP_KERNEL);
    if(!priv)
    {
        return -ENOMEM;
    }

    /*---------------------注册 字符设备部分-----------------*/
    /*1.分配主次设备号，这里即支持静态指定，也至此动态申请*/
    if(dev_major != 0)
    {
        // 静态
        devno = MKDEV(dev_major, 0);
        rv = register_chrdev_region(devno, DEV_CNT, DEV_NAME);  // /proc/devices/sht20
    }
    else
    {
        // 动态
        rv = alloc_chrdev_region(&devno, 0, DEV_CNT, DEV_NAME); // 动态申请字符设备号
        dev_major = MAJOR(devno);      // 获取主设备号
    }

    if(rv < 0)
    {
        printk("%s driver can't get major %d\n", DEV_NAME, dev_major);
        return rv;
    }
    printk(" %s driver use major %d\n", DEV_NAME, dev_major);

    /*2.分配cdev结构体，绑定主次设备号、fops到cdev结构体中，并注册给Linux内核*/
    priv->cdev.owner = THIS_MODULE;
    cdev_init(&priv->cdev, &sht20_fops); /*初始化cdev,把fops添加进去*/
    rv = cdev_add(&priv->cdev, devno, DEV_CNT); /*注册给内核,设备数量1个*/

    if(rv != 0)
    {
        printk(" %s driver can't register cdev:result=%d\n", DEV_NAME, rv);
        goto undo_major;
    }
    printk("%s driver can register cdev:result=%d\n", DEV_NAME, rv);

    //3.创建类，驱动中进行节点创建
    priv->dev_class = class_create(THIS_MODULE, DEV_NAME);
    if(IS_ERR(priv->dev_class))
    {
        printk("%s driver create class failure.\n", DEV_NAME);
        rv = -ENOMEM;
        goto undo_cdev;
    }

    //4.创建设备
    devno = MKDEV(dev_major, 0);
    priv->dev = device_create(priv->dev_class, NULL, devno, NULL, DEV_NAME);    // 注册/dev/sht20这个设备节点

    if(IS_ERR(priv->dev_class))
    {
        rv = -ENOMEM;    //返回错误码,应用空间strerror查看
        goto undo_class;
    }

    //5.创建sysfs文件初始化
    if(device_create_file(priv->dev, &dev_attr_temp_humi))
    {
        rv = -ENOMEM;
        goto undo_device;
    }

    //6.保存私有数据
    priv->client = client;
    i2c_set_clientdata(client, priv);
    dev_set_drvdata(priv->dev, priv);
    dev_info(&client->dev, "sht20 i2c driver probe okey.\n");

    return 0;

undo_device:
    device_destroy(priv->dev_class, devno);  //返回错误码,应用空间strerror查看

undo_class:
    class_destroy(priv->dev_class);

undo_cdev:
    cdev_del(&priv->cdev);

undo_major:
    unregister_chrdev_region(devno, DEV_CNT);

    return rv;
}

static int sht20_remove(struct i2c_client *client)
{
    /* 删除设备 */
    struct sht20_priv   *priv = i2c_get_clientdata(client);

    dev_t devno = MKDEV(dev_major, 0);

    // 删除sys中的属性
    device_remove_file(priv->dev, &dev_attr_temp_humi);

    // 注销每一个设备号
    device_destroy(priv->dev_class, devno);

    // 注销类
    class_destroy(priv->dev_class);

    // 删除cdev
    cdev_del(&priv->cdev);

    // 释放设备号
    unregister_chrdev_region(devno, DEV_CNT);

    devm_kfree(&client->dev, priv); // 释放堆
    printk("sh20 driver removed\n");
    return 0;
}

/*定义设备树匹配表*/
static const struct of_device_id sht20_of_match_table[] = {
    {.compatible = "imx_i2c_sht20"},
    {/* sentinel */}
};

/*定义i2c总线设备结构体*/
struct i2c_driver sht20_driver = {
    .probe = sht20_probe,
    .remove = sht20_remove,
    .driver = {
        .name = "imx_i2c_sht20", //无设备树时，用于设备和驱动间匹配
        .owner = THIS_MODULE,
        .of_match_table = sht20_of_match_table,
    },
};

/*
 * 驱动初始化函数
 */
 static int __init sht20_driver_init(void)
 {
    int ret;
    printk("sht20_driver_init\n");
    ret = i2c_add_driver(&sht20_driver); // 添加一个i2c设备驱动
    return ret;
}

/*
 * 驱动注销函数
 */
static void __exit sht20_driver_exit(void)
{
    printk("sht20_driver_exit\n");
    i2c_del_driver(&sht20_driver);  // 删除一个i2c设备驱动
}

module_init(sht20_driver_init);
module_exit(sht20_driver_exit);

MODULE_AUTHOR("NongJieYing");
MODULE_DESCRIPTION("I2C_sht20 driver on i.MX6ULL platform");
MODULE_LICENSE("GPL");
MODULE_ALIAS("i2c:sht20");