#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/seq_file.h>
#include <linux/cdev.h>

#include <linux/uaccess.h>	/* copy_*_user */

#include "scull.h"		/* local definitions */


int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_quantum = SCULL_QUANTUM;
int scull_qset =    SCULL_QSET;

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);


MODULE_AUTHOR("Liu Wenliang");
MODULE_LICENSE("Dual BSD/GPL");


struct scull_dev *scull_devices;


/**
 *  trim() 通过遍历链表将所有找到的量子和量子集释放。
 *  open()的时候调用，模块的清除函数也要调用
 */
static int scull_trim(struct scull_dev *dev){

    struct scull_qset *next ,*dptr;

    int qset = dev->qset;
    int i;

    for(dptr = dev->data ; dptr ; dptr = next){
        if(dptr->data){
            for(i = 0;i< qset;i++){
                kfree(dptr->data[i]);
            }
            kfree(dptr->data);
            dptr->data = NULL;
        }

        next = dptr->next;
        kfree(dptr);
    }

    dev->size = 0;
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    dev->data = NULL;

    return 0;
}


/**
 *  scull将cdev初始化并添加到系统中 ,初始化内核和设备的接口cdev
 */ 
static void scull_setup_cdev(struct scull_dev *dev , int index){

    int err,devno = MKDEV(scull_major,scull_minor + idnex);

    cdev_init( &dev->cdev , &scull_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scull_fops;

    err = cdev_add(&dev->cdev , devno ,1);

    if(err){
        printk(KERN_NOTICE "Error %d adding scull %d \n",err,index);
    }


}

/**
 *  复写file_operations内容
 */ 

static loff_t scull_llseek(struct file *filp , loff_t offset , int orig){
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


struct scull_qset *scull_follow(struct scull_dev *dev , int n){

    struct scull_qset *qs = dev->data;

    if(!qs){
        qs = dev->data = kmalloc(sizeof(struct scull_qset) , GFP_KERNEL);
        if(qs == NULL){
            return NULL;
        }

        memset(qs, 0 ,sizeof(struct scull_qset));
    }

    while(n--){
        if(!qs->next){

            qs->next = kmalloc(sizeof(struct scull_qset) , GFP_KERNEL);
            if(qs->next == NULL){
                return NULL;
            }

            memset(qs->next , 0 , sizeof(struct scull_qset));
        }

        qs = qs->next;
        continue;
    }

    return qs;

}




static ssize_t scull_read(struct file *filp,char __user *buf , size_t size, loff_t *ppos){

    struct scull_dev *dev = filp->private_data;

    // 一次处理一个量子的数据
    struct scull_qset *dptr; // 第一个链表项目

    int quantum = dev->quantum , qset = dev->qset;
    int itemsize = quantum * qset;
    int item , s_pos, q_pos , ret;
    ssize_t retval = 0;

    if(mutex_lock_interruptible(&dev->mutex)){
        return -ERESTARTSYS;
    }

    if( *ppos >= dev->size){
        goto out;
    }
    if( *ppos + size > dev->size){
        // 不足size的大小了，只读剩下的全部
        size = dev->size - *ppos;    
    }

    // 在量子集中寻找链表项、qset索引以及偏移量
    item = (long)*ppos / itemsize;
    ret  = (long)*ppos % itemsize;

    s_pos = ret / quantum;
    q_pos = ret % quantum;

    // 沿链表前行，直到找到正确的位置
    dptr = scull_follow(dev,item);

    if(dptr == NULL || !dptr->data || !dptr->data[s_pos] ){
        goto out;
    }
    

    // 读取该量子的数据直到末尾
    if(size > quantum - q_pos){
        size = quantum - q_pos;
    }

    if(copy_to_user(buf , dptr->data[s_pos]+q_pos , size)){
        ret = -EFAULT;
        goto out;
    }

    *ppos += size;
    ret = size;
    

out:
    mutex_unlock(&dev->mutex);
    return ret;






    // Song的read函数

    
    // 要读的位置相较于文件开头的偏移，如果偏移大于 GLOBALMEM_SIZE 意味着已经到达文件末尾
    // unsigned long p = *ppos;

    // unsigned int count = size;
    // int ret = 0;
    // if(p >= GLOBALMEM_SIZE){
    //     return 0;
    // }

    // if(count > GLOBALMEM_SIZE - p){
    //     count = GLOBALMEM_SIZE - p;
    // }

    // if(copy_to_user(buf, dev->mem + p,count)){
    //     /* Bad address */
    //     ret = -EFAULT;
    // }else {
    //     *ppos += count;
    //     ret = count;
    //     printk(KERN_INFO "read %u bytes(s) from %lu\n", count, p);
    // }

    // return ret;

}


static ssize_t scull_write(struct file *filp,const char __user *buf , size_t size, loff_t *ppos){

    struct scull_dev *dev = filp->private_data;

    // 一次处理一个量子的数据
    struct scull_qset *dptr; // 第一个链表项目

    int quantum = dev->quantum , qset = dev->qset;
    int itemsize = quantum * qset;
    int item , s_pos, q_pos , ret;
    ssize_t retval = -ENOMEM;

    if(mutex_lock_interruptible(&dev->mutex)){
        return -ERESTARTSYS;
    }


    // 在量子集中寻找链表项、qset索引以及偏移量
    item = (long)*ppos / itemsize;
    ret  = (long)*ppos % itemsize;

    s_pos = ret / quantum;
    q_pos = ret % quantum;

    // 沿链表前行，直到找到正确的位置
    dptr = scull_follow(dev,item);

    if(dptr == NULL ){
        goto out;
    }

    if( !dptr->data || !dptr->data[s_pos] ){
        dptr->data = kmalloc(qset * sizeof(char *) , GFP_KERNEL );
        
        if( !dptr->data){
            goto out;
        }
        memset(dptr->data, 0 , qset * sizeof(char *) );
    }
	if (!dptr->data[s_pos]) {
		dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
		if (!dptr->data[s_pos])
			goto out;
	}

    
    // 将数据写入该量子
    if(size > quantum - q_pos){
        size = quantum - q_pos;
    }

    if(copy_from_user( dptr->data[s_pos]+q_pos ,buf , size)){
        ret = -EFAULT;
        goto out;
    }

    *ppos += size;
    ret = size;
    
    // 更新文件大小
    if (dev->size < *ppos){
        dev->size = *ppos;
    }


out:
    mutex_unlock(&dev->mutex);
    return ret;
    


    



    // // Song的write函数
    // // 要读的位置相较于文件开头的偏移，如果偏移大于GLOBALMEM_SIZE 意味着已经到达文件末尾
    // unsigned long p = *ppos;

    // unsigned int count = size;
    // int ret = 0;

    // struct globalmem_dev *dev = filp->private_data;

    // if(p >= GLOBALMEM_SIZE){
    //     return 0;
    // }

    // if(count > GLOBALMEM_SIZE - p){
    //     count = GLOBALMEM_SIZE - p;
    // }

    // if(copy_from_user(dev->mem + p, buf ,count)){
    //     /* Bad address */
    //     ret = -EFAULT;
    // }else {
    //     *ppos += count;
    //     ret = count;
    //     printk(KERN_INFO "written %u bytes(s) from %lu\n", count, p);
    // }

    // return ret;

}

static long scull_ioctl(struct file *filp , unsigned int cmd ,unsigned long arg){

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

static int scull_open(struct inode *inode,struct file *filp){

    // 便于处理多设备
    struct scull_dev *dev; // 获取device的信息

    dev = container_of(inode->i_cdev, struct scull_dev , cdev);

    filp->private_data = dev; // 

    // globalmem中是没有的
    // scull_trim是用来释放数据区的，open()的时候也可以不用调用
    if( (filp->f_flags & O_ACCMODE ) == O_WRONLY){
        if(mutex_lock_interruptible(&dev->mutex)){
            return -ERESTARTSYS;
        }

        scull_trim(dev); // 忽略错误
        mutex_unlock(&dev->mutex);
    }
   
    return 0;
}

static int scull_release(struct inode *inode,struct file *filp){
    return 0;
}


/**
 * 
 * 模块初始化内容、模块卸载内容 
 */ 
int scull_init_module(void){

    int result , i;
    dev_t dev = 0;

    // scull.c 获取主设备号的代码  P52
    if(scull_major){
        dev = MKDEV(scull_major,scull_minor);
        result = register_chrdev_region(dev,scull_nr_devs,"scull");
    }else{
        result = alloc_chrdev_region(&dev , scull_minor, scull_nr_devs , "scull");
        scull_major  =MAJOR(dev);
    }

    if(result < 0){
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return result;
    }




}



// scull 的file_operatinos结构初始化如下 ,P57
struct file_operations scull_sngl_fops = {
	.owner =    THIS_MODULE,
	.llseek =   scull_llseek,
	.read =     scull_read,
	.write =    scull_write,
	.unlocked_ioctl =    scull_ioctl,
	.open =     scull_open,
	.release =  scull_release,
};




module_init(scull_init_module);
module_exit(scull_cleanup_module);