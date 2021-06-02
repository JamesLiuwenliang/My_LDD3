
// 用pipe实现


#include "scull_05.h"


// 包括两个等待队列和一个缓冲区
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

    // 无数据读取
    while(dev->rp == dev->wp){
        // 释放锁
        up(&dev->sem);
        if(filp->flags & O_NONBLOCK)
            return -EAGAIN;
        
		PDEBUG("\"%s\" reading: going to sleep\n", current->comm);

        // 进入睡眠状态
        if(wait_event_interruptible(&dev->inq , (dev->rp != dev->wp)))
            return -ERESTARTSYS;
        
        // 此时并不能判断数据是否可以被获得
        // 但首先获取信号量
        if(down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        
    }

    // 判断写区和读区的位置,不要读区到写区,如果写区在读区后方,读区就读到dev的末尾结束
    if(dev->wq > dev->rq){
        count = min(count , (size_t)(dev->wq - dev->rp));
    }else{
        // 写入指针回卷,返回数据直到dev->end
        count = min(count , (size_t)(dev->end - dev->rp));
    }

    // 拷贝开始
    if(copy_to_user(buf , dev->rp , count)){
        // 结束后释放锁
        up(&dev->sem);
        return -EFAULT;
    }

    dev->rp += count;
    if(dev->rp == dev->end){
        // 读到末尾了
        dev->rp = dev->buffer;
    }

    up(&dev->sem);

    // 最后 唤醒所有写入者并返回
    wait_up_interruptible(&dev->outq);
	PDEBUG("\"%s\" did read %li bytes\n",current->comm, (long)count);
    return count;    

}

// 判断有多少空间被释放
static int spacefree(struct scull_pipe* dev){
    if(dev->rp == dev->wp){
        return dev->buffersize - 1;
    }

    return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}

static ssize_t scull_p_write(struct file* filp,const char __user* buf, size_t count, loff_t* f_pos){

    struct scull_pipe* dev = filp->private_data;
    int result;

    if(down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    // 确保有空间可写入,即确保函数有可用的缓冲空间
    result = scull_getwritespace(dev , filp);
    if(result)
        return result; // scull_getwritespace会调用 up(&dev->sem)

    // 有空间可用,进行数据接收
    count = min(count , (size_t)spacefree(dev));
    if(dev->wp >= dev->rp){
        count = min(count , (size_t)dev->end - dev->wp);
    }else{
        // 数据回卷,填充到rp - 1
        count = min(count , (size_t)(dev->rp - dev->wp - 1));
    }

	PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, dev->wp, buf);

    if(copy_from_user(dev->wp , buf , count)){
        up(&dev->sem);
        return -EFAULT;
    }

    dev->wp += count;

    if(dev->wp == dev->end){
        dev->wp = dev->buffer; // 回卷
    }

    up(&dev->sem);

    wake_up_interruptible(&dev->inq); // 阻塞在read和select上

    // 通知异步读取者
    if(dev->async_queue){
        kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
    }

	PDEBUG("\"%s\" did write %li bytes\n",current->comm, (long)count);
	return count;

}



int scull_p_init(dev_t firstdev){

    int i , result;
    result = register_chrdev_region(firstdev , scull_p_nr_devs , "scullp");

    if(result < 0){
        printk(KERN_NOTICE "Unable to get scullp region, error %d\n", result);
        return 0;
    }

    scull_p_devno = firstdev;
    scull_p_devices = kmalloc(scull_p_nr_devs * sizeof(struct scull_pipe) , GFP_KERNEL);

    if(scull_p_devices == NULL){
        unregister_chrdev_region(firstdev , scull_p_nr_devs);
        return 0;
    }

    memset(scull_p_devices , 0, scull_p_nr_devs * sizeof(struct scull_pipe));
    for(i = 0; i < scull_p_nr_devs ; i++){
        init_waitqueue_head( &(scull_p_devices[i].inq) );
        init_waitqueue_head( &(scull_p_devices[i].outq) );
        mutex_init(&scull_p_devices[i].mutex);
        scull_p_setup_cdev(scull_p_devices + i, i);
    }

    return scull_p_nr_devs;
}

void scull_p_cleanup(void){
    int i;

    // 没有需要释放的
    if(!scull_p_devices)
        return;

    for(i = 0; i < scull_p_nr_devs; i++ ){
        cdev_del( &scull_p_devices[i].cdev);
        kfree(scull_p_devices[i].buffer);
    }

    kfree(scull_p_devices);

    unregister_chrdev_region(scull_p_devno , scull_p_nr_devs);
    scull_p_devices = NULL;
}





