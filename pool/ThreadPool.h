// filename ：ThreadPool.h
#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include “Locker.h”
/*线程池类， 将它定义为模板是为了代码复用。 模板参数T是任务类*/
template< typename T>
class ThreadPool
{
public:
    /* 参数number是线程池中的数量， MAXREQ 是请求队列中最多允许的，等待处理的请求的数量 */
    ThreadPool(int number = 8, int MAXREQ = 10000);
    ~ThreadPool();
    /* 往请求队列中添加任务 */
    bool Append(T *request);
private:
    /* 工作线程运行的函数， 它不断从工作队列中取出任务并执行之
     为啥是static 函数呢 ？
    */
    static void* Worker(void* arg);
    void Run();
private:
    int m_threadNumber; /* 线程池中的线程数 */
    int m_request;      /* 请求队列中允许的最大请求数 */
    pthread_t* m_threads;/* 描述线程池的数组，其大小为m_threadNumber */
    std::list< T* > m_workerQueue; /* 请求队列 */
    Locker m_queueLocker; /* 保护请求队列的互斥锁 */
    sem m_queueStat;      /* 是否有任务需要处理 */
    bool m_stop;          /* 是否结束线程 */
};

template<typename T>
ThreadPool<T>::ThreadPool(int number, int MAXREQ) :
    m_threadNumber(number),m_request(MAXREQ), m_stop(false), m_threads(NULL)
{
    if((m_threadNumber <= 0) || (m_request <=0)) {
        throw std::exception();
    }
    
    m_threads = new pthread_t[m_request ];
    if(! m_threads) {
        throw std::exception();
    }
    /* 创建 thread_number 个线程，并将它们都设置为脱离线程 */
    for(int i=0 ;i <m_threadNumber ; ++i) {
        printf("create the %dth thread\n", i);
        if(pthread_create(m_threads + i, NULL, Worker, this) !=0) {
            delete [] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])) {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
ThreadPool<T>::~ThreadPool()
{
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool ThreadPool<T>::Append(T* request)
{
    /* 操作工作队列时一定要加锁， 因为它被所有线程共享 */
    m_queueLocker.lock();
    if( m_workerQueue.size() > m_request)
    {
        m_queueLocker.unlock();
        return false;
    }
    m_workerQueue.push_back(request);
    m_queueLocker.unlock();
    m_queueStat.post();
    return true;
}

template<typename T>
void* ThreadPool<T>::Worker(void* arg)
{
    ThreadPool* pool = (ThreadPool*)arg;
    pool->Run();
    return pool;
}

template<typename T>
void ThreadPool<T>::Run()
{
    while( ! m_stop)
    {
        m_queueStat.wait();
        m_queueLocker.lock();
        if(m_workerQueue.empty()) {
            m_queueLocker.unlock();
            continue;    
        }
        T* request = m_workerQueue.front();
        m_workerQueue.pop_front();
        m_queueLocker.unlock();
        if (! request) {
            continue;
        }
        request->process();
    }
}
#endif //THREADPOOL_H