#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define GLOBALMEM_SIZE 0x1000
#define MEM_CLEAR 0x1
#define GLOBALMEM_MAJOR 230

static int globalmem_major = GLOBALMEM_MAJOR;
module_param(globalmem_major,int , S_IRUGO);

struct globalmem_dev
{
    /* data */
    struct cdev cdev;
    unsigned char mem[GLOBALMEM_SIZE];

};


struct globalmem_dev *globalmem_devp;

/**
 * open()
 */

static int globalmem_open(struct inode *inode,struct file *filp){
    filp->private_data = globalmem_devp;
    return 0;
}

/**
 *  释放全部的设备
 */
static int globalmem_release(struct inode *inode,struct file *filp){
    return 0;
}

static int just_test(){
    
    return 0;

}


/**
 * globalmem 设备驱动的读写函数
 */
static ssize_t globalmem_read(struct file *filp,char __user *buf , size_t size, loff_t *ppos){

    // 要读的位置相较于文件开头的偏移，如果偏移大于GLOBALMEM_SIZE 意味着已经到达文件末尾
    unsigned long p = *ppos;

    unsigned int count = size;
    int ret = 0;

    struct globalmem_dev *dev = filp->private_data;

    if(p >= GLOBALMEM_SIZE){
        return 0;
    }

    if(count > GLOBALMEM_SIZE - p){
        count = GLOBALMEM_SIZE - p;
    }

    if(copy_to_user(buf, dev->mem + p,count)){
        /* Bad address */
        ret = -EFAULT;
    }else {
        *ppos += count;
        ret = count;
        printk(KERN_INFO "read %u bytes(s) from %lu\n", count, p);
    }

    return ret;

}

static ssize_t globalmem_write(struct file *filp,const char __user *buf , size_t size, loff_t *ppos){

    // 要读的位置相较于文件开头的偏移，如果偏移大于GLOBALMEM_SIZE 意味着已经到达文件末尾
    unsigned long p = *ppos;

    unsigned int count = size;
    int ret = 0;

    struct globalmem_dev *dev = filp->private_data;

    if(p >= GLOBALMEM_SIZE){
        return 0;
    }

    if(count > GLOBALMEM_SIZE - p){
        count = GLOBALMEM_SIZE - p;
    }

    if(copy_from_user(dev->mem + p, buf ,count)){
        /* Bad address */
        ret = -EFAULT;
    }else {
        *ppos += count;
        ret = count;
        printk(KERN_INFO "written %u bytes(s) from %lu\n", count, p);
    }

    return ret;

}

/**
 *  ioctl(): 一个专门用来接受命令的函数，当接收到MEM_CLEAR命令后，会将全局内存的有效数据长度清0，这里关于IO的操作可以参考<Song P148>
 *  比较推荐命令码为幻数,<Song P147>
 */ 
static long globalmem_ioctl(struct file *filp , unsigned int cmd ,unsigned long arg){

    struct globalmem_dev *dev = filp->private_data;
    switch(cmd){
        case MEM_CLEAR:
            memset(dev->mem , 0, GLOBALMEM_SIZE);
            printk(KERN_INFO "globalmem is set to zero\n");
            break;

        // 日后可以添加命令
 
        default:
            return -EINVAL;
    }

    return 0;
}

/**
 * seek函数:用来修改文件的当前读写位置，并将新的位置作为（正的）返回值返回
 *      对文件的定位的起始位置可以是文件开头、当前位置、和文件尾
 *      先判断参数是否合法，合法时更新文件的当前位置并返回该位置
 */
static loff_t globalmem_llseek(struct file *filp , loff_t offset , int orig){
    loff_t ret = 0;
    switch(orig){

        // 从文件开头位置seek
        case 0:
            if((offset < 0) || ((unsigned int)offset > GLOBALMEM_SIZE)){
                ret = -EINVAL;
                break;
            }
            filp->f_pos = (unsigned int) offset;
            ret = filp->f_pos;
            break;

        // 从文件开头位置开始seek
        case 1:
            if( ((filp->f_pos + offset) > GLOBALMEM_SIZE ) || ((filp->f_pos + offset) < 0)){
                ret = -EINVAL;
                break;
            }
            filp->f_pos += offset;
            ret = filp->f_pos;
            break;
        
        default:
            ret = -EINVAL;
            break;

    }

    return ret;


}


// globalmem 设备驱动文件的操作结构体
// 编写globalmem就是要实现这些函数
static const struct file_operations globalmem_fops = {
    .owner = THIS_MODULE,
    .llseek = globalmem_llseek,
    .read = globalmem_read,
    .write = globalmem_write,
    .unlocked_ioctl = globalmem_ioctl,
    .open = globalmem_open,
    .release = globalmem_release,
};


/**
 *  模块加载啊的一些初始化函数
 */ 

// 完成cdev的初始化和添加
static void globalmem_setup_cdev(struct globalmem_dev *dev , int index){

    int err, devno = MKDEV(globalmem_major,index);

    cdev_init(&dev->cdev , & globalmem_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev , devno , 1);

    if(err){
        printk(KERN_NOTICE "Error %d adding globalmem %d",err, index);
    }
}


// 模块加载函数
static int  __init globalmem_init(void){
    int ret;

    dev_t devno = MKDEV(globalmem_major, 0);

    // 设备号申请
    if(globalmem_major){
        ret = register_chrdev_region(devno ,1 , "globalmem");
    }else{
        ret = alloc_chrdev_region(&devno , 0,1 ,"globalmem");
        globalmem_major = MAJOR(devno);
    }

    if(ret < 0){
        return ret;
    }

    globalmem_devp = kzalloc(sizeof(struct globalmem_dev), GFP_KERNEL);

    if(!globalmem_devp){
        ret = -ENOMEM;
        goto fail_malloc;
    }

    globalmem_setup_cdev(globalmem_devp, 0);
    return 0;

fail_malloc:
    unregister_chrdev_region(devno,1);
    return ret;    

}

module_init(globalmem_init);

// module退出部分
static void __exit globalmem_exit(void){

    cdev_del(&globalmem_devp->cdev);
    kfree(globalmem_devp);
    unregister_chrdev_region(MKDEV(globalmem_major,0),1);
}

module_exit(globalmem_exit);

MODULE_AUTHOR("Liu Weliang");
MODULE_LICENSE("GPL v2");
