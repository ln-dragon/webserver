#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <list>
#include "lock.h"

template <typename T>
class threadpool{
public:
    threadpool(int thread_number = 8, int m_max_requests = 10000);
    ~threadpool();
    bool append(T* request);//添加任务
    void run();//线程池运行
private:
    static void* work(void* arg);
    //线程数量
    int m_thread_number;
    //线程池数组
    pthread_t* m_threads;
    //请求队列
    std::list<T*> m_workqueue;
    //请求队列最大请求数
    int m_max_requests;
    //互斥锁
    mutex m_queuemutex;
    //信号量用来是否有任务需要处理
    sem m_queuestat;
    //是否结束
    bool m_stop;
};

template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL){
    if(thread_number <= 0 || max_requests <= 0){
        throw std::exception();
    }
    //开辟内存创建线程池
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }
    for(int i=0; i<m_thread_number; i++){
        //创建线程
        // printf( "create the %dth thread\n", i);
        if(pthread_create(m_threads+i, NULL, work, this) != 0){//将this作为本类对象传递到work中
            delete [] m_threads;
            throw std::exception();
        }
        //分离线程，可以让其子线程自己回收
        if(pthread_detach(m_threads[i]) != 0){
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append(T* request){
    //先要加锁
    m_queuemutex.lock();
    if(m_workqueue.size() > m_max_requests){
        m_queuemutex.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuemutex.unlock();
    m_queuestat.post();//信号量增加 
    return true;
}

template <typename T>
void* threadpool<T>::work(void* arg){
    threadpool* pool = (threadpool *)arg;
    pool->run();
}

template <typename T>
void threadpool<T>::run(){
    while( !m_stop){
        m_queuestat.wait();
        m_queuemutex.lock();
        if(m_workqueue.empty()){
            m_queuemutex.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuemutex.unlock();
        if( !request){
            continue;
        }
        //线程池处理读取出来的HTTP请求数据
        printf("线程池处理从套接字中读取出来的HTTP请求数据\n");
        request->process();
    }
}
#endif