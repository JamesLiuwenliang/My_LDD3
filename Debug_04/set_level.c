
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/klog.h>


// 如果设置为1的时候，之后只有级别为0的才能达到控制台
int main(int argc,  char **argv){
    int level;

    if(argc == 2){
        level = atoi(argv[1]);
    }else{
        fprintf(stderr , "%s need a single arg\n",argv[0]);
    }

    if(klogctl(8,NULL , level) < 0){
        fprintf(stderr , "%s: syslog(setlevel): %s \n" ,argv[0],strerror(errno));
        exit(1);
    }

    exit(0);


}