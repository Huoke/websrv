/*******************************************
* 封装3中线程同步机制，信号量、互斥锁、条件变量
********************************************/ 
#ifndef LOCKER_H
#define LOCKER_H
#include <exception>
#include <pthread.h>
#include <semaphore.h>
/*封装信号量的类*/ 
class Sem
{
public:
    Sem()
    {
        if(sem_init(&m_sem, 0, 0) != 0) {
            // 构造函数没有返回值，可以通过刨除异常来报告错误
            throw std::exception();
        }
    }
    ~Sem() 
    {
        sem_destory(&m_sem);
    }

    /*等待信号量 */
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }

    /* 增加信号量 */
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

/* 封装互斥锁类 */
class Locker
{
public:
    // 创建并初始化互斥锁
    Locker()
    {
        if(pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~Locker()
    {
        pthread_mutex_destory(&m_mutex);
    }

    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }    
private:
    pthread_mutex_t m_mutex;    
};

/* 封装条件变量 */
class Cond
{
public:
private:

};
#endif //LOCKER_H
