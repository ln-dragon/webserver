#ifndef LOCK_H
#define LOCK_H
#include <exception>
#include <semaphore.h>
#include <pthread.h>

//信号量类
class sem{
public:
    sem(){
        if(sem_init(&m_sem, 0, 0) != 0){
            throw std::exception();
        }
    }
    ~sem(){
        sem_destroy(&m_sem);
    }
    //等待信号量
    bool wait(){
        return sem_wait(&m_sem) == 0;
    }
    //增加信号量
    bool post(){
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

//互斥锁类
class mutex{
public:
    mutex(){
        if(pthread_mutex_init(&m_mutex, NULL) != 0){
            throw std::exception();
        }
    }
    ~mutex(){
        pthread_mutex_destroy(&m_mutex);
    }
    //加锁
    bool lock(){
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    //解锁
    bool unlock(){
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
private:
    pthread_mutex_t m_mutex;
};

//条件变量类
class cond{
public:
    cond(){
        if(pthread_cond_init(&m_cond, NULL) != 0){
            throw std::exception();
        }
    }
    ~cond(){
        pthread_cond_destroy(&m_cond);
    }
    //等待
    bool wait(pthread_mutex_t *m_mutex){
        int ret = 0;
        //枷锁
        pthread_mutex_lock(m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //解锁
        pthread_mutex_unlock(m_mutex);
        return  ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t){//含超时时间
        int ret = 0;
        //加锁
        pthread_mutex_lock(m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //解锁
        pthread_mutex_unlock(m_mutex);
        return  ret == 0;
    }
    //唤醒
    bool signal(){
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast(){
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;
    // pthread_mutex_t m_mutex;
};
#endif 