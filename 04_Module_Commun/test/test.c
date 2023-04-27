/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  test.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月18日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月18日 06时39分31秒"
 *                 
 ********************************************************************************/


#include <linux/init.h>
#include <linux/module.h>
#include "add_sub.h"

static long a =1;
static long b =1;
static int AddOrSub = 1;
static __init int test_init(void)
{
	long result = 0;
	printk(KERN_ALERT "test init\n");
	if(AddOrSub == 1)
	{
		result = add_integer(a, b);
	}
	else
	{
		result = sub_integer(a, b);
	}
	printk(KERN_ALERT "The %s result is %ld\n", AddOrSub==1?"Add":"Sub", result);
	return 0;
}

static __exit void test_exit(void)
{
	printk(KERN_ALERT "test exit\n");
}

module_init(test_init);
module_exit(test_exit);

module_param(a, long, S_IRUGO);
module_param(b, long, S_IRUGO);
module_param(AddOrSub, int, S_IRUGO);

//描述信息
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("NongJieYing <njy_roxy@outlook.com>");
MODULE_DESCRIPTION("The module for testing module params and EXPORT_SYMBOL");
MODULE_VERSION("V1.0");
