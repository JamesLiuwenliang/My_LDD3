
#ifndef _SCULL_H_
#define _SCULL_H_

#include <linux/ioctl.h> 

#undef PDEBUGG
#define PDEBUGG(fmt, args...) /* nothing: it's a placeholder */

#ifndef SCULL_MAJOR
#define SCULL_MAJOR 0   /* dynamic major by default */
#endif

#ifndef SCULL_NR_DEVS
#define SCULL_NR_DEVS 4 // scull0 -> scull3
#endif

#ifndef SCULL_P_NR_DEVS
#define SCULL_P_NR_DEVS 4 // scullpipe0 -> scullpipe3
#endif

/*
 * The bare device is a variable-length region of memory.
 * Use a linked list of indirect blocks.
 *
 * "scull_dev->data" points to an array of pointers, each
 * pointer refers to a memory area of SCULL_QUANTUM bytes.
 *
 * The array (quantum-set) is SCULL_QSET long.
 */
#ifndef SCULL_QUANTUM
#define SCULL_QUANTUM 4000
#endif

#ifndef SCULL_QSET
#define SCULL_QSET    1000
#endif


/*
 * scull_qset 和 scull_dev 
 * scull_qset 相当于构成链表的操作
 */
struct scull_qset {
	void **data; // 指向量子集(即很多很多量子)
	struct scull_qset *next;
};

// scull_dev用来表示设备
struct scull_dev{
    struct scull_qset *data; // 指向第一个量子集的指针
    int quantum;             // 当前量子的大小
    int qset;                // 当前数组的大小 
    unsigned long size;      // 保存在其中的数据总量
    unsigned int access_key; // 由 sculluid 和 scullpriv 使用
    struct semaphore sem;    // 互斥信号量
    struct mutex mutex;     /* mutual exclusion semaphore     */
    struct cdev cdev;        // 字符设备结构
};


extern int scull_nr_devs;
extern int scull_major;
extern int scull_quantum;
extern int scull_qset;


int scull_open(struct inode *inode , struct file* filp);
int scull_release(struct inode* inode , struct file* filp);
int scull_trim(struct scull_dev* dev);
ssize_t scull_read(struct file* filp , char __user *buf , size_t count, loff_t* f_pos);
ssize_t scull_write(struct file* filp ,const char __user* buf, size_t count , loff_t* f_pos);
struct scull_qset *scull_follow(struct scull_dev *dev, int n);
long scull_ioctl(struct file* filp , unsigned int cmd , unsigned long arg);
loff_t scull_llseek(struct file* filp, loff_t off , int where);

void scull_cleanup_module(void);

/*
 * Ioctl definitions
 */
/* Use 'k' as magic number */
#define SCULL_IOC_MAGIC  'k'
/* Please use a different 8-bit number in your code */
#define SCULL_IOCRESET    _IO(SCULL_IOC_MAGIC, 0)

#define SCULL_IOCSQUANTUM _IOW(SCULL_IOC_MAGIC,  1, int)
#define SCULL_IOCSQSET    _IOW(SCULL_IOC_MAGIC,  2, int)
#define SCULL_IOCTQUANTUM _IO(SCULL_IOC_MAGIC,   3)
#define SCULL_IOCTQSET    _IO(SCULL_IOC_MAGIC,   4)
#define SCULL_IOCGQUANTUM _IOR(SCULL_IOC_MAGIC,  5, int)
#define SCULL_IOCGQSET    _IOR(SCULL_IOC_MAGIC,  6, int)
#define SCULL_IOCQQUANTUM _IO(SCULL_IOC_MAGIC,   7)
#define SCULL_IOCQQSET    _IO(SCULL_IOC_MAGIC,   8)
#define SCULL_IOCXQUANTUM _IOWR(SCULL_IOC_MAGIC, 9, int)
#define SCULL_IOCXQSET    _IOWR(SCULL_IOC_MAGIC,10, int)
#define SCULL_IOCHQUANTUM _IO(SCULL_IOC_MAGIC,  11)
#define SCULL_IOCHQSET    _IO(SCULL_IOC_MAGIC,  12)


#define SCULL_IOC_MAXNR 14

#endif /* _SCULL_H_ */