#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "provider.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DaeyoonHong");
MODULE_DESCRIPTION("Consumer module: calls symbols exported by provider");

static int __init consumer_init(void)
{
    int sum;

    printk(KERN_INFO "consumer: module loaded\n");

    sum = provider_add(3, 4);
    printk(KERN_INFO "consumer: provider_add(3, 4) = %d\n", sum);

    sum = provider_add(10, 20);
    printk(KERN_INFO "consumer: provider_add(10, 20) = %d\n", sum);

    sum = provider_add(55, 55);
    printk(KERN_INFO "consumer: provider_add(55, 55) = %d\n", sum);

    printk(KERN_INFO "consumer: provider call count = %d\n",
           provider_get_call_count());

    return 0;
}

static void __exit consumer_exit(void)
{
    printk(KERN_INFO "consumer: module unloaded\n");
}

module_init(consumer_init);
module_exit(consumer_exit);
