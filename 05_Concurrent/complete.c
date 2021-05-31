#include <linux/module.h>
#include <linux/init.h>

#include <linux/sched.h>  /* current and everything */
#include <linux/kernel.h> /* printk() */
#include <linux/fs.h>     /* everything... */
#include <linux/types.h>  /* size_t */
#include <linux/completion.h>


MODULE_AUTHOR("Liu Wenliang");
MODULE_LICENSE("Dual BSD/GPL");

static int complete_major = 0;

struct file_operations complete_fops = {
    .owner = THIS_MODULE,
    .read = complete_read,
    .write = complete_write,
};

// 创建一个 completion
DECLARE_COMPLETION(comp);

// 对于completion的使用,应是写操作优先于读操作,写操作执行之后通知读操作
ssize_t complete_read (struct file* filp , char __user* buf , size_t count  , loff_t* pos){
    printk(KERN_DEBUG , "process %i (%s) going to sleep\n",current->pid, current->comm);
    // 发送请求
    wait_for_completion(&comp);
    printk(KERN_DEBUG , "awoken %i (%s)\n" ,current->pid , current->comm);
    return 0;
}

ssize_t complete_write(struct file* filp , const char __user* buf , size_t count , loff_t* pos){
    printk(KERN_DEBUG , "process %i (%s) going to sleep\n",current->pid, current->comm);
    completion(&comp);
    return count;
}


int complete_init(void){
    int result;

    result = register_chrdev(complete_major , "complete" , &complete_fops);

    if(result < 0){
        return result;
    }

    if(complete_major == 0){
        complete_major = result;
    }

    return 0;
}

int complete_cleanup(void){
    unregister_chrdev(complete_major,"complete");
}

module_init(complete_init);
module_exit(complete_cleanup);

