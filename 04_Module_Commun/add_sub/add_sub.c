/*********************************************************************************
 *      Copyright:  (C) 2023 Noah<njy_roxy@outlook.com>
 *                  All rights reserved.
 *
 *       Filename:  add_sub.c
 *    Description:  This file 
 *                 
 *        Version:  1.0.0(2023年04月18日)
 *         Author:  Noah <njy_roxy@outlook.com>
 *      ChangeLog:  1, Release initial version on "2023年04月18日 06时18分26秒"
 *                 
 ********************************************************************************/


#include <linux/init.h>
#include <linux/module.h>
#include "add_sub.h"

long add_integer(long a, long b)			//函数返回a+b的和
{
	return a+b;
}

long sub_integer(long a, long b)			//函数返回a和b的差
{
	return a-b;
}

EXPORT_SYMBOL(add_integer);				//导出加法函数
EXPORT_SYMBOL(sub_integer);			//导出减法函数

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("NongJieYing <njy_roxy@outlook.com>");

