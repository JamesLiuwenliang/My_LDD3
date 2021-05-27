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

#include <linux/seq_file.h> // 重写seq_file接口

/**
 * 主要重写 start next stop 和 show xiangdangyu 相当于实现了一个迭代器
struct seq_file {
    char *buf;
	size_t size;
	size_t from;
	size_t count;
	loff_t index;
	loff_t version;
	struct mutex lock;
	const struct seq_operations *op;
	void *private;
};
 */
#include "scull.h"

static struct seq_operations scull_seq_ops = {
    .start = scull_seq_start , 
    .next = scull_seq_next , 
    .stop = scull_seq_stop,
    .show = scull_seq_show
};

static struct file_operation scull_proc_ops = {
    .owner = THIS_MODULE , 
    .open = scull_proc_open, 
    .read = seq_read , 
    .llseek = seq_lseek , 
    .release = seq_release
};

// 只单独指定了file_operation下的open()函数,其他的函数依然使用已经定义好的
static int scull_proc_open(struct inode *inode , struct file* file){
    return seq_open(file , &scull_seq_ops);
}


static int scull_seq_show(struct seq_file* s ,void *v){
    struct scull_dev *dev = (struct scull_dev* )v;
    struct scull_qset *d;

    int i;
    if(down_interruptible(&dev->sem)){
        return -ERESTARTSYS;
    }

    seq_printf(s,"\n Device %i :qset %i , q %i , sz %li\n" ,(int)dev->quantum , dev->size);

    for(d = dev->data ; d ; d = d->next){
        seq_printf(s , "item at %p , qset at %p \n ",d , d->data);
        if(d->data && !d->next){
            for(i = 0 ; i< dev->qset ; i++){
                if(d->data[i]){
                    seq_printf(s , "    %  4i: %8p \n" , i , d->data[i]);
                }
            }
        }
    }

    up(&dev->sem);
    return 0;

}

static void *scull_seq_start (struct seq_file *s , loff_t *pos){
    if(*pos > scull_nr_devs){
        return NULL;
    }    

    return scull_devices + *pos;
}

static void* scull_seq_next(struct seq_file* s , void *v , loff_t *pos){

    (*pos)++;
    if( *pos >= scull_nr_devs){
        return NULL;
    }

    return scull_devices + *pos;
}

void scull_seq_stop(struct seq_file *s , void *v);


// 相当于实现了read_proc(),但是假定不会生成多于一页的数据,因此并没有用上start和offset的值
int scull_read_procmem(char *buf  ,char **start , off_t offset , int count , int *eof , void *data){
    int i , j , len = 0;
    int limit  = count - 80; // 不要打印超过这个值的数据

    for(i = 0; i< scull_nr_devs && len <= limit ;i++){
        struct scull_dev *d = &scull_devices[i];
        struct scull_qset *qs = d->data;

        if(down_interruptible(&d->sem))
            return -ERESTARTSYS;

        len += sprintf(buf + len , "\n Device %i: qset %i , q %i , sz %li\n",
                i  ,d->qset , d->quantum , d->size);
        for( ; qs && len <= limit ; qs = qs->next){
            len += sprintf(buf+len , "item at %p , qset at %p \n", qs , qs->data);

            if(qs->data && !qs->next){

                for(j = 0 ;j<d->qset ; j++){
                    if(qs->data[j]){
                        len += sprintf(buf+len , "   % 4i: %8p\n" , j,qs->data[j]);
                    }
                }

            }
        }
        up(&scull_devices[i].sem);
    }

    *eof = 1;
    return len;


}

