#include <linux/init.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/time.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <asm/hardirq.h>

/**********************************
 *  该模块用于获取当前时间
 * 
 * // 获取当前时间
 * - $ head -8 /proc/currentime
**********************************/


// 默认的参数,默认值为1000
int delay = HZ;

module_param(delay , int , 0);

MODULE_AUTHOR("Liu Wenliang");
MODULE_LICENSE("Dual BSD/GPL");

enum jit_files{

    JIT_BUSY,
    JIT_SCHED,
    JIT_SCHED,
    JIT_QUEUE,
    JIT_SCHEDTO
};


/*
 * 显示当前时间
 */ 
static int jit_currenttime_proc_show(struct seq_file* m , void* v){
    struct timeval tv1;
    struct timespec tv2;
    unsigned long j1;
    u64 j2;

    j1 = jiffies;
    j2 = get_jiffies_64();

    do_gettimeofday(&tv1);
    tv2 = current_kernel_time();

  	seq_printf(m,"0x%08lx 0x%016Lx %10i.%06i\n"
	       "%40i.%09i\n",
	       j1, j2,
	       (int) tv1.tv_sec, (int) tv1.tv_usec,
	       (int) tv2.tv_sec, (int) tv2.tv_nsec);
	return 0;
 
}

static int jit_currenttime_proc_open(struct inode* inode , struct file* filp){
    return single_open(file , jit_currenttime_proc_show , NULL);
}

static const struct file_operations jit_currentime_proc_fops = {
    .open     = jit_currenttime_proc_open,
    .read     = seq_read,
    .llseek   = seq_lseek,
    .release  = single_release,
};

int tdelay = 10;
module_param(tdelay , int , 0);







static const struct file_operations jit_timer_proc_fops = {
    .open    = jit_timer_proc_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

static __init jit_init(void){

    proc_create("currentime", 0 , NULL , &jit_currentime_proc_fops);
    proc_create_data("jitbusy"  , 0, NULL, &jit_fn_proc_fops , (void*)JIT_BUSY);
    proc_create_data("jitsched" , 0, NULL, &jit_fn_proc_fops , (void*)JIT_SCHED);
    proc_create_data("jitqueue" , 0, NULL, &jit_fn_proc_fops , (void*)JIT_QUEUE);
    proc_create_data("jitschedto",0, NULL, &jit_fn_proc_fops , (void*)JIT_SCHEDTO);

    proc_create("jitimer", 0, NULL, &jit_timer_proc_fops);
    proc_create("jitasklet",0, NULL,&jit_tasklet_proc_fops);
    proc_create_data("jitasklethi",0,NULL, &jit_tasklet_proc_fops, (void*)1);

    return 0;
}

static __exit jit_cleanup(void){

    remove_proc_entry("currentimt",NULL);
    remove_proc_entry("jitbusy",NULL);
    remove_proc_entry("jitsched",NULL);
    remove_proc_entry("jitqueue",NULL);
    remove_proc_entry("jitschedto",NULL);

	remove_proc_entry("jitimer", NULL);
	remove_proc_entry("jitasklet", NULL);
	remove_proc_entry("jitasklethi", NULL);

    printk(KERN_ALERT "MyTime messure is over\n");
}


module_init(jit_init);
module_exit(jit_cleanup);