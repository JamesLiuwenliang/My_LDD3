#include <linux/kernel.h> /* printk() */
#include <linux/module.h>
#include <linux/slab.h>   /* kmalloc() */
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/fs.h>     /* everything... */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/fcntl.h>
#include <linux/cdev.h>
#include <linux/tty.h>
#include <asm/atomic.h>
#include <linux/list.h>
#include <linux/cred.h>

#include "scull.h"

static dev_t scull_a_firstdev;

static atomic_t scull_s_available = ATOMIC_INIT(1);

// 单用户
static int scull_s_open(struct inode* inode  , struct file* filp){
    struct scull_dev* dev = &scull_s_device; // 设备信息
    if( !atomic_dec_and_test(&scull_s_available) ){
        atomic_inc(&scull_s_available);
        return -EBUSY;
    }

    if( (filp->f_flags & O_ACCMODE) == O_WRONLY){
        scull_trim(dev);
    }

    filp->private_data = dev;
    return 0;
}

static int scull_s_release(struct inode* inode,struct file* filp){

    atomic_inc(&scull_s_available);
    return 0;
}

// 多进程并发访问,但每次只允许一个用户打开该设备
static int scull_u_open(struct inode* inode  , struct file* filp){
    struct scull_dev* dev = &scull_s_device; // 设备信息

    spin_lock(&scull_u_lock);

    if(scull_u_count &&
        (scull_u_owner != current->uid) && // 允许用户
        (scull_u_owner != current->euid) && // 允许用户执行 su 命令的用户
        !capable(CAP_DAC_OVERRIDE)) // 也允许root用户
    {
  
        spin_unlock(&scull_u_lock);
        return -EBUSY;
    
    }

    if(scull_u_count == 0){
        scull_u_owner = current->uid; // 获得所有者
    }

    scull_u_count++;    
    spin_unlock(&scull_u_lock);



    if( (filp->f_flags & O_ACCMODE) == O_WRONLY){
        scull_trim(dev);
    }

    filp->private_data = dev;
    return 0;
}

static int scull_u_release(struct inode* inode , struct file*filp){
    spin_lock(&scull_u_lock);
    scull_u_count --;
    spin_unlock(&scull_u_lock);
    return 0;
}




