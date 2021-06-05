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

/*******************************************************
 * 单用户访问模式,当然每次只有一个用户访问
*******************************************************/ 
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

/*******************************************************
 * 多进程并发访问,但每次只允许一个用户打开该设备
*******************************************************/ 
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


/*******************************************************
 * 替代 EBUSY 的 阻塞性open
 * 相较于 scull_u_open ,不会返回 -EBUSY ,而是会等待设备
*******************************************************/

static struct scull_dev scull_w_device;
static int scull_w_count;
static kuid_t scull_w_owner;
static DECLARE_WAIT_QUEUE_HEAD(scull_w_wait);
static DEFINE_SPINLOCK(scull_w_lock);

static inline int scull_w_available(void){
    return scull_w_count == 0 ||
        uid_eq(scull_w_owner , current_uid()) ||
        uid_eq(scull_w_owner , current_euid()) ||
        capable(CAP_DAC_OVERRIDE);
}

static int scull_w_open(struct inode* inode , struct file* filp){
    struct scull_dev* dev = &scull_w_device;

    spin_lock(&scull_w_lock);

    while(!scull_w_available()){
        spin_unlock(&scull_w_lock());
        if(filp->f_flags & O_NONBLOCK) 
            return -EAGAIN;
        
        // 加入阻塞队列
        if(wait_event_interruptble(scull_w_wait , scull_w_available()))
            return -ERESTARTSYS;
        spin_lock(&scull_w_lock);
    }

    if(scull_w_count == 0)
        scull_w_owner = current_uid();
    scull_w_count++;
    spin_unlock(&scull_w_lock);

    if( (filp->f_flags & O_ACCMODE) == O_WRONLY)
        scull_trim(dev);
    filp->private_data = dev;
    return 0;
}

static int scull_w_release(struct inode* inode , struct file* filp){

    int temp;

    spin_lock(&scull_w_lock);
    scull_w_count--;
    temp = scull_w_count;
    spin_unlock(&scull_w_lock);

    if(temp == 0){
        // 唤醒所有等待的进程
        wake_up_interruptible_sync(&scull_w_wait);
    }

    return 0;
}

/*******************************************************
 * 打开时复制设备
*******************************************************/ 
// 和复制相关的设备结构包括一个key成员
struct scull_listitem{
    struct scull_dev device;
    dev_t key;
    struct list_head list;
}

// 设备的链表,以及保护它的锁
static LIST_HEAD(scull_c_list);
static spinlock_t scull_c_lock = SPIN_LOCK_UNLOCKED;

// 查找设备,如果没有就创建一个
static struct scull_dev* scull_c_lookfor_device(dev_t key){
    struct scull_listitem* lptr;

    list_for_each_entry(lptr , &scull_c_list , list){
        if(lptr->key == key){
            return &list(lptr->device);
        }
    }

    // 没有找到,自己创建设备
    lptr = kmalloc(sizeof(struct scull_listitem) , GFP_KERNEL);
    if(!lptr){
        return NULL;
    }

    //  初始化该设备
    memset(lptr , 0 , sizeof(struct scull_listitem));
    lptr->key = key;
    scull_trim( &(lptr->device) ); // 初始化
    init_MUTEX( &(lptr->device.sem));

    // 将其放入链表中
    list_add(&lptr->list , &scull_c_list);
    return &(lptr->device);
}

static int scull_c_open(struct inode* inode , struct file* filp){
    struct scull_dev* dev;

    dev_t key;

    if(!current->signal->tty){
        PDEBUG("Process \"%s\" has no ctl tty\n", current->comm);
		return -EINVAL;
    }

    key = tty_devnum(current->signal->tty);

    // 在链表中查找 scullc 设备
    spin_lock( &scull_c_lock);
    dev = scull_c_lookfor_device(key);
    spin_unlock( &scull_c_lock );

    if(!dev)
        return -ENOMEM;
    

	if ( (filp->f_flags & O_ACCMODE) == O_WRONLY)
		scull_trim(dev);
	filp->private_data = dev;
	return 0;
}

static int scull_c_release(struct inode *inode, struct file *filp)
{
    // 没有做太多特殊处理,最后关闭的时候释放设备,这个代码其实没有加一个打开设备的计数器
	return 0;
}


struct file_operations scull_priv_fops = {
	.owner =    THIS_MODULE,
	.llseek =   scull_llseek,
	.read =     scull_read,
	.write =    scull_write,
	.unlocked_ioctl =    scull_ioctl,
	.open =     scull_c_open,
	.release =  scull_c_release,
};



