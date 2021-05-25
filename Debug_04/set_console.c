#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>

// 重定向控制台消息
// setconsole这个程序就是用来选择专门用来接收内核消息的控制台
// 在后续的设置是stdin,即标准输入设备作为专门来接收内核消息的设备
// 附加的参数用作即将要指定接收消息的控制台的编号(即编号代指虚拟控制台)
int main(int argc , char **argv){

    // 11 是TIOCLINUX的命令编号
    // bytes[1] 存放选定的控制台编号
    char bytes[2] = {11,0};
    if(argc == 2){
        bytes[1] = atoi(argv[1]);
    }else{
        fprintf(stderr, "%s: need a single arg\n",argv[0]); exit(1);
    }

    // STDIN_FILENO 标准输入设备,可以认为就是stdin
    if(ioctl(STDIN_FILENO , TIOCLINUX , bytes) < 0){
        fprintf(stderr , "%s: ioctl(stdin , TIOCLINUX): %s \n" ,argv[0],strerror(errno));
        exit(1);
    }
    exit(0);

}