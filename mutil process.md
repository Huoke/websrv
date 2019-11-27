# 多进程编程
进程是Linux操作系统环境的基础， 它控制着系统上几乎所有的活动。
- 复制进程映像的fork系统调用和替换进程映像的exec系列系统调用
- 僵尸进程以及如何避免僵尸进程
- 进程间通信(IPC) 最简单的方式：管道
- 3 中System V进程间通信方式：信号量、消息队列、共享内存。它们都是有AT&T System V2版本的UNix引入的，所以称为 System V IPC。
- 在进程间传递文件描述符的通用方法： 通过UNIX本地域socket传递特殊的辅助数据
# 一、 fork 系统调用
```C
#include <sys/types.h>
#include <unistd.h>
 pid_t fork(void);
```
1. 函数每次调用都返回两次， 在父进程中返回的是子进程的PID， 在子进程中返回的则是0。

2. 返回值是判断当前进程是父进程还是子进程的依据。

3. fork 失败返回 -1，并设置errno。

4. fork函数复制当前进程，在内核进程表中创建一个新的进程表项。

5. **新的进程表项有很多属性和原进程相同，例如：堆指针， 栈指针和标志寄存器的值。 **

6. **但也有许多属性被赋予了新的值， 例如：该进程的PPID被设置为原进程的PID， 信号位图被清除(原进程设置的信号处理函数不再对新进程起作用)。**

7. 子进程的代码与父进程完全相同，同时它还会复制父进程的数据(堆数据、 栈数据、静态数据)。

8. 数据复制采用的是所谓的写时复制(copy on write)， 即只有在任一进程(父进程或者子进程)对数据执行了写操作时， 复制才会发生(先是缺页中断，然后操作系统给子进程分配内存并复制父进程的数据)。即便如此， 如果我们在程序中分配了大量内存，那么使用fork时也应该十分谨慎， 尽量避免没有必要的内存分配和数据复制。

9. 此外， 创建子进程后， 父进程中打开的文件描述符默认在子进程中也是打开的， 且文件描述符的引用计数器加 1。 不仅如此， 父进程的用户根目录、 当前工作目录等变量的引用计数器都会加 1。

# 二、exec 系列系统调用

有时候我们需要在子进程中执行其他程序， 也就是替换当前映像，这就需要使用如下exec系列函数之一：
```C
#include <unistd.h>

extern char **environ;

int execl(const char *path, const char *arg, .../* (char  *) NULL */);

int execlp(const char *file, const char *arg, .../* (char  *) NULL */);

int execle(const char *path, const char *arg, ..., (char *) NULL, char * const envp[]);

int execv(const char *path, char *const argv[]);

int execvp(const char *file, char *const argv[]);

int execvpe(const char *file, char *const argv[], char *const envp[]);
```

- path: 参数指定可执行文件的完整路径。
- file：参数可以接受文件名。该文件的具体位置则在环境变量PATH中搜索。
- arg：接受可变参数， argv则接受参数数组， 它们都会被传递给新程序(path 或者file 指定的程序)的main 函数。
- envp：参数用于设置新程序的环境变量。如果没有设置新程序的环境变量， 则新程序将使用由全局变量environ指定的环境变量。

一般情况下， exec函数是不返回的， 除非出错。 它出错时返回-1，并设置errno。

**如果没有出错， 则原程序中exec调用之后的代码将不会执行， 因为原程序已经被exec的参数指定的程序完全替换(包括代码和数据)。**

exec 函数不会关闭原程序打开的文件描述符，除非该文件描述符被设置了类似SOCK_CLOEXEC的属性。


