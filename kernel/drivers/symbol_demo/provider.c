#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DaeyoonHong");
MODULE_DESCRIPTION("Provider module: exports symbols for other modules");

static int call_count;

/*
 * 다른 모듈이 호출할 수 있도록 EXPORT_SYMBOL로 심볼을 내보낸다.
 * static을 붙이면 안 된다 (외부에서 링크되어야 하므로).
 */
int provider_add(int a, int b)
{
    call_count++;
    printk(KERN_INFO "provider: add(%d, %d) called (count=%d)\n",
           a, b, call_count);
    return a + b;
}
EXPORT_SYMBOL_GPL(provider_add);

int provider_get_call_count(void)
{
    return call_count;
}
EXPORT_SYMBOL_GPL(provider_get_call_count);

static int __init provider_init(void)
{
    printk(KERN_INFO "provider: module loaded\n");
    return 0;
}

static void __exit provider_exit(void)
{
    printk(KERN_INFO "provider: module unloaded\n");
}

module_init(provider_init);
module_exit(provider_exit);
