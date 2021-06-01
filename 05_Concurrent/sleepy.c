
// 实现功能: 任何试图从该设备上读取的进程均被置于休眠状态.
//          只要某个进程向该设备写入,所有休眠的进程都会被唤醒
#include <linux/module.h>
#include <linux/init.h>

#include <linux/sched.h>  /* current and everything */
#include <linux/kernel.h> /* printk() */
#include <linux/fs.h>     /* everything... */
#include <linux/types.h>  /* size_t */
#include <linux/wait.h>

MODULE_AUTHOR("JingXing");
MODULE_LICENSE("Dual BSD/GPL");


static int sleepy_major = 0;

static DECLARE_WAIT_QUEUE_HEAD (wq);
static int flag = 0;

ssize_t sleepy_read(struct file* filp , char __user* buf , size_t count , loff_t* pos){

    printk(KERN_DEBUG "process %i (%s) going to sleepy\n",current->pid , current->comm);
    wait_event_interruptible(wq , flag != 0);
    flag = 0;

    printk(KERN_DEBUG "awoken %i (%s) \n",current->pid , current->comm);

    return 0;
}

ssize_t sleepy_write(struct file* filp , const char __user* buf , size_t count , loff_t* pos){

    printk(KERN_DEBUG "process %i (%s) awakening the readers...\n",current->pid , current->comm);
    flag = 1;
    wait_event_interruptible(&wq);

    // 成功避免重试
    return count;
}

struct file_operations sleepy_fops = {
    .owner = THIS_MODULE,
    .read = sleepy_read,
    .write = sleepy_write,
};


void sleepy_init(){
    int result;

    // 注册设备号,采用动态分配的方式(推荐)

    result = register_chrdev(sleepy_major , "sleepy" , &sleepy_fops);
    if(result < 0){
        return result;
    }

    if(sleepy_major == 0){
        sleepy_major = result;
    }

    return 0;

}

void sleepy_cleanup(void){
    unregister_chrdev(sleepy_major , "sleepy");
}

module_init(sleepy_init);
module_exit(sleepy_cleanup);







