#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
MODULE_LICENSE("Dual BSD/GPL");


// $ sudo insmod hellop.ko howmany = 10 whom = "MOM"
static char *whom = "world";
static int howmany = 1;
module_param(howmany,int ,S_IRUGO);
module_param(whom ,charp,S_IRUGO);


static int hello_init(void){

    int i ;
    for(i = 0;i < howmany ;i++){
        printk("Hello %s\r\n",whom);
    }

    // KERN_ALERT 是消息的优先级,后面是没有","的
    // 优先级级别仅次于KERN_EMERG,在使用的时候会被当做宏来替换成字符串,所以不能有逗号
    printk(KERN_ALERT "Hello World\n");
    return 0;
}

static void hello_exit(void){

    printk(KERN_ALERT "ByeBye\n");

}



module_init(hello_init);
module_exit(hello_exit);

