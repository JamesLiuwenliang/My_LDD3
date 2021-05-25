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

#include "scull.h"

/**
 *  需要初始化的时候再使用
 */ 
int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS;
int scull_quantum = SCULL_QUANTUM;
int scull_qset    = SCULL_QSET;

module_param(scull_major,int ,S_IRUGO);
module_param(scull_minor,int ,S_IRUGO);
module_param(scull_nr_devs,int ,S_IRUGO);
module_param(scull_quantum,int ,S_IRUGO);
module_param(scull_qset,int ,S_IRUGO);

MODULE_AUTHOR("Liu Wenliang");
MODULE_LICENSE("Dual BSD/GPL");

struct scull_dev* scull_devices;



// 这次的scull设备驱动程序所实现的只是最重要的设备方法,下列的这些方法要被重新实现
static struct file_operations scull_fops = {
    .owner  = THIS_MODULE,
    .llseek = scull_llseek, // 修改文件的当前读写位置
    .read   = scull_read,   // 从设备读取文件
    .write  = scull_write,  // 向设备发送数据
    .unlocked_ioctl  = scull_ioctl,  // 系统调用,提供了一种执行设备特定命令的方法
    .open   = scull_open,   // 打开文件，对设备文件执行的第一个操作
    .release = scull_release, // 当file结构被释放时，调用这个操作
};


// 将cdev(字符设备)添加到scull系统上
static void scull_setup_cdev(struct scull_dev* dev , int index){
    int err , devno = MKDEV(scull_major , scull_minor + index);

    cdev_init(&dev->cdev , &scull_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scull_fops;
    err = cdev_add(&dev->cdev , devno ,1);

    if(err){
        printk(KERN_NOTICE "Error %d adding scull %d" ,err , index);
    }


}

/**
 * 下面的scull_open是个简化版本
 * - 分配并填写置于filp->private_data里的数据结构
 */ 
int scull_open(struct inode *inode , struct file* filp){

    struct scull_dev *dev; // 设备信息

    // 这个宏是帮助实现解析inode所包含的参数,然后返回给dev设备信息
    dev = container_of(inode->i_cdev , struct scull_dev , cdev);
    filp->private_data = dev; // private_data 是一个void * ,方便日后在别的方法下访问

    // 如果设备只写,将设备长度截取为0
    if( (filp->f_flags & O_ACCMODE) == O_WRONLY){
        if (mutex_lock_interruptible(&dev->mutex))
			return -ERESTARTSYS;
        scull_trim(dev);

        mutex_unlock(&dev->mutex);
    }

    return 0;
}

/**
 * 释放需要关闭的硬件
 */ 
int scull_release(struct inode* inode , struct file* filp){
    return 0;
}

/**
 * scull_trim 负责释放整个数据区,并在文件以写入方式打开时由scull_open调用
 * scull_trim 通过遍历链表，释放所有找到的量子和量子集
 * 模块的清楚函数也使用scull_trim,以便将scull所使用的内存返回给系统
 */
int scull_trim(struct scull_dev* dev){
    struct scull_qset* next , *dptr;
    int qset = dev->qset;

    int i;
    for(dptr = dev->data; dptr ; dptr = next){
        
        if(dptr->data){
            for(i = 0; i < qset; i++){
                kfree(dptr->data[i]);
            }

            kfree(dptr->next);
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
 * read():dev->user,从设备拷贝数据到用户空间(需要使用 copy_to_user)
 */ 
ssize_t scull_read(struct file* filp , char __user *buf , size_t count, loff_t* f_pos){

    struct scull_dev* dev = filp->private_data;
    struct scull_qset *dptr; // 第一个链表项
    int quantum = dev->quantum, qset = dev->qset; // 量子数 和 量子集数量
    int itemsize = quantum * qset; // 该链表项有多少个字节
    int item , s_pos , q_pos , rest;

    ssize_t retval = 0;

    if(down_interruptible(&dev->sem)){
        return -ERESTARTSYS;
    }

    if(*f_pos >= dev->size)
        goto out;
    // 不足count的大小了
    if(*f_pos + count > dev->size)
        count = dev->size - *f_pos;
    
    // 在量子集中寻找链表项，qset索引以及偏移量
    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;

    // 沿链表前行，直到找到正确的位置
    dptr = scull_follow(dev , item);

    if(dptr == NULL || !dptr->data || !dptr->data[s_pos])
        goto out;

    // 读取该量子的数据直到末尾
    if(count > quantum - q_pos)
        count = quantum - q_pos;
    
    if(copy_to_user(buf , dptr->data[s_pos] + q_pos , count)){
        retval = -EFAULT;
        goto out;
    }

    *f_pos += count;
    retval = count;

out:
    up(&dev->sem);
    return retval;

}

/**
 * write():user->dev,从用户空间写入设备中(需要使用 copy_from_user)
 * 每次只处理一个量子
 */ 
ssize_t scull_write(struct file* filp ,const char __user* buf, size_t count , loff_t* f_pos){

    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum , qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos ,q_pos , rest;

    ssize_t retval = -ENOMEM;
    
    if(down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;

    dptr = scull_follow(dev, item);
    
    if(dptr == NULL)
        goto out;

    if(!dptr->data){
        dptr->data = kmalloc(qset * sizeof(char *) ,  GFP_KERNEL);

        if(!dptr->data)
            goto out;
        
        memset(dptr->data,  0,qset * sizeof(char*));
    }

    if(!dptr->data[s_pos]){
        dptr->data[s_pos] = kmalloc(quantum , GFP_KERNEL);
        if(!dptr->data[s_pos])
            goto out;
    }

    if(count > quantum - q_pos)
        count = quantum - q_pos;

     
    if(copy_from_user( dptr->data[s_pos] + q_pos , buf, count)){
        retval = -EFAULT;
        goto out;
    }

    *f_pos += count;
    retval = count;

    if(dev->size < *f_pos)
        dev->size = *f_pos;

out:
    up(&dev->sem);
    return retval;   

}

struct scull_qset *scull_follow(struct scull_dev *dev, int n){

    struct scull_qset* qs = dev->data;

    if(!qs){
        qs = dev->data = kmalloc(sizeof(struct scull_qset) , GFP_KERNEL);
        if(qs == NULL)
            return NULL;
        
        memset(qs, 0 ,sizeof(struct scull_qset));
    }

    // Then follow the list
    while(n--){

        if( !qs->next){
            qs->next = kmalloc(sizeof(struct scull_qset) , GFP_KERNEL);
            if(qs->next == NULL)
                return NULL;
            
            memset(qs->next , 0, sizeof(struct scull_qset));
        }

        qs = qs->next;
        continue;
    }

    return qs;


}

long scull_ioctl(struct file* filp , unsigned int cmd , unsigned long arg){

    int err = 0 , tmp ;
    int retval = 0;

    // 错误指令
    if(_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
    if(_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;

	switch(cmd) {

	  case SCULL_IOCRESET:
		scull_quantum = SCULL_QUANTUM;
		scull_qset = SCULL_QSET;
		break;
        
	  case SCULL_IOCSQUANTUM: /* Set: arg points to the value */
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		retval = __get_user(scull_quantum, (int __user *)arg);
		break;

	  case SCULL_IOCTQUANTUM: /* Tell: arg is the value */
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		scull_quantum = arg;
		break;

	  case SCULL_IOCGQUANTUM: /* Get: arg is pointer to result */
		retval = __put_user(scull_quantum, (int __user *)arg);
		break;

	  case SCULL_IOCQQUANTUM: /* Query: return it (it's positive) */
		return scull_quantum;

	  case SCULL_IOCXQUANTUM: /* eXchange: use arg as pointer */
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		tmp = scull_quantum;
		retval = __get_user(scull_quantum, (int __user *)arg);
		if (retval == 0)
			retval = __put_user(tmp, (int __user *)arg);
		break;

	  case SCULL_IOCHQUANTUM: /* sHift: like Tell + Query */
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		tmp = scull_quantum;
		scull_quantum = arg;
		return tmp;
        
	  case SCULL_IOCSQSET:
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		retval = __get_user(scull_qset, (int __user *)arg);
		break;

	  case SCULL_IOCTQSET:
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		scull_qset = arg;
		break;

	  case SCULL_IOCGQSET:
		retval = __put_user(scull_qset, (int __user *)arg);
		break;

	  case SCULL_IOCQQSET:
		return scull_qset;

	  case SCULL_IOCXQSET:
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		tmp = scull_qset;
		retval = __get_user(scull_qset, (int __user *)arg);
		if (retval == 0)
			retval = put_user(tmp, (int __user *)arg);
		break;

	  case SCULL_IOCHQSET:
		if (! capable (CAP_SYS_ADMIN))
			return -EPERM;
		tmp = scull_qset;
		scull_qset = arg;
		return tmp;

        /*
         * The following two change the buffer size for scullpipe.
         * The scullpipe device uses this same ioctl method, just to
         * write less code. Actually, it's the same driver, isn't it?
         */



	  default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;


}


// 重新定位文件位置
loff_t scull_llseek(struct file* filp, loff_t off , int where){
    struct scull_dev* dev = filp->private_data;
    loff_t new_pos;

    switch(where){
        case 0: // SEEK_SET
            new_pos = off;
            break;
        case 1: // SEEK_CUR
            new_pos = filp->f_pos + off;
            break;
        case 2: // SEEK_END
            new_pos = dev->size + off;
            break;
        default:
            return -EINVAL;
    }

    if(new_pos < 0)
        return -EINVAL;
    filp->f_pos = new_pos;
    return new_pos;


}



int scull_init_module(void){
    int res , i;
    dev_t dev = 0;

    if(scull_major){

        // 已有主次设备号
        dev = MKDEV(scull_major,scull_minor);
        res = register_chrdev_region(dev , scull_nr_devs,"scull");
    }else{

        // 随机分配设备号
        res = alloc_chrdev_region(&dev , scull_minor , scull_nr_devs,"scull");
        scull_major = MAJOR(dev);
    }

    if(res < 0){
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return res;
    }

    scull_devices = kmalloc(scull_nr_devs  * sizeof(struct scull_dev), GFP_KERNEL);
    if(!scull_devices){
        res = -ENOMEM;
        goto fail;
    }

    memset(scull_devices , 0, scull_nr_devs * sizeof(struct scull_dev));

    for(i = 0;i< scull_nr_devs;i++){
        scull_devices[i].quantum = scull_quantum;
        scull_devices[i].qset = scull_qset;
        mutex_init(&scull_devices[i].mutex);
        scull_setup_cdev(&scull_devices[i], i);
    }

    dev = MKDEV(scull_major, scull_minor + scull_nr_devs);

	return 0; /* succeed */

  fail:
	scull_cleanup_module();
	return res;

}

void scull_cleanup_module(){
    int i ;
    dev_t devno = MKDEV(scull_major ,scull_minor);

    // 丢弃字符设备
    if(scull_devices){
        for(i = 0;i< scull_nr_devs;i++){
            scull_trim(scull_devices + i);
            cdev_del(&scull_devices[i].cdev);
        }
        kfree(scull_devices);
    }

    /* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, scull_nr_devs);

	/* and call the cleanup functions for friend devices */
	// scull_p_cleanup();
	// scull_access_cleanup();

}





module_init(scull_init_module);
module_exit(scull_cleanup_module);
