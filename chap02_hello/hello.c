#include <linux/init.h>
#include <linux/module.h>

MODULE_LICENSE("Dual BSD/GPL");

static int hello_init(void){

    // KERN_ALERT 是消息的优先级,后面就是没有","
    printk(KERN_ALERT "Hello World\n");
    return 0;
}

static void hello_exit(void){

    printk(KERN_ALERT "Over\n");

}

module_init(hello_init);
module_exit(hello_exit);

