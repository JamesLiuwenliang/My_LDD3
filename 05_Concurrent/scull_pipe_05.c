
// 用pipe实现


#include "scull_05.h"



struct scull_pipe{
    
    wait_queue_head_t inq , outq; // 读取和写入序列
    char *buffer , *end;  // 缓冲区的起始和结尾
    int buffersize;  // 用于指针运算
    char *rp , *wq;  // 读取和写入的位置
    int nreaders , nwriters; // 读写、打开的数量
    struct fasync_struct* async_queue; // 异步读取者
    struct semaphore sem ;  // 互斥信号量
    struct cdev cdev;  // 字符设备结构

}

// 这里的read()支持阻塞性和非阻塞性输入
static ssize_t scull_p_read(struct file* filp , char __user* buf , size_t count, loff_t* f_pos){
    struct scull_pipe* dev = filp->private_data;

    if(down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    while(dev->rp == dev->wp){
        // 释放锁
        up(&dev->sem);
        if(filp->flags & O_NONBLOCK)
            return -EAGAIN;
        
		PDEBUG("\"%s\" reading: going to sleep\n", current->comm);
        if(wait_event_interruptible(&dev->inq , (dev->rp != dev->wp)))
            return -ERESTARTSYS;
        
        // 否则循环,但首先获取锁
        if(down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        
    }

    // 数据已就绪,返回
    if(dev->wq > dev->rq){
        count = min(count , (size_t)(dev->wq - dev->rp));
    }else{
        // 写入指针回卷,返回数据直到dev->end
        count = min(count , (size_t)(dev->wp - dev->rp));
    }

    if(copy_to_user(buf , dev->rp , count)){
        up(&dev->sem);
        return -EFAULT;
    }

    dev->rp += count;
    if(dev->rp == dev->end){
        dev->rp = dev->buffer;
    }

    up(&dev->sem);

    // 最后 唤醒所有写入者并返回
    wait_up_interruptible(&dev->outq);
	PDEBUG("\"%s\" did read %li bytes\n",current->comm, (long)count);
    return count;    

}





