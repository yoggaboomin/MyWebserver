#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include <locker.h>
#include <cstdio>

// 线程池类 定义成模版类，为了代码复用 模版参数T是任务类
template <typename T>
class threadpool
{
public:
    threadpool(int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request);

private:
private:
    //线程数量
    int m_thread_number;

    //线程池子数组，大小为m_thread_number  动态的创建数组
    pthread_t *m_threads;

    // 请求队列中最多允许的，等待处理的请求数量
    int m_max_requests;

    // 请求队列
    std::list<T *> m_workqueue;

    // 互斥锁
    locker m_queuelocker;

    // 信号量 用来判断是否有任务需要处理
    sem m_queuestat;

    //是否结束线程
    bool m_stop;
};
template <typename T>
threadpool<T>::threadpool(int thread_number = 8, int max_request = 10000) : m_thread_number(thread_number), m_max_requests(max_request)
{
    if (thread_number <= 0 || max_request <= 0)
    {
        throw std::exception();
    }
    // 析构的时候要销毁
    m_threads = new pthread_t[thread_number];

    if (!m_threads)
    {
        throw std::exception();
    }

    //创建thread_number个线程 ，设置线程脱离

    for (int i = 0; i < thread_number; ++i)
    {
        printf("creat the &dth thread\n",i);

        // worker 必须是static
        if(pthread_create(m_threads + i,NULL,worker,NULL) != 0){
            delete[] m_threads;
            throw std:: exception();
        }
        if(phtread_detatch(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template<typename T>
threadpool<T>::~threadpool(){
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T> ::append(T* request){
    //操作工作队列的时候 先加锁，因为他被所有线程共享
    m_queuelocker.lock();
    if(m_workqueue.size > m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); //增加一个信号量
    return true;
}


#endif