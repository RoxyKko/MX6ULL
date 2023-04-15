/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  hello.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月15日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月15日 06时08分24秒"
 *                 
 ********************************************************************************/
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static __init int hello_init(void)
{
	printk(KERN_ALERT "hello world\n");

	return 0;
}

static __exit void hello_exit(void)
{
	printk(KERN_ALERT "Goodbye\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("NongJieYing <njy_roxy@outlook.com>");
