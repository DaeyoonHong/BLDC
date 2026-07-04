#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DaeyoonHong");
MODULE_DESCRIPTION("Hello world module for BBB kernel driver environment verification");

static int __init hello_init(void)
{
    printk(KERN_INFO "hello_world: module loaded\n");
    return 0;
}

static void __exit hello_exit(void)
{
    printk(KERN_INFO "hello_world: module unloaded\n");
}

module_init(hello_init);
module_exit(hello_exit);
