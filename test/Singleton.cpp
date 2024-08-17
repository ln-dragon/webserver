#include <iostream>
#include <mutex>
//恶汉模式
class TaskQueue{
public:
    TaskQueue(const TaskQueue& ) = delete;
    static TaskQueue* getinstance(){
        return tq;
    }
private:
    TaskQueue() = default;
    static TaskQueue* tq;
};
// 静态成员初始化放到类外部处理
TaskQueue* TaskQueue::tq = new TaskQueue();

// 懒汉模式
class TaskQueue1{
public:
    TaskQueue1(const TaskQueue1&) = delete;
    static TaskQueue1* getinstance(){
        if(tq == nullptr){
            m_mutex.lock();
            if(tq == nullptr){
                tq = new TaskQueue1();
            }
            m_mutex.unlock();
        }
        return tq;
    }
private:
    static std::mutex m_mutex;
    static TaskQueue1* tq;
    TaskQueue1() = default;
};
//静态成员初始化放到类外部处理
TaskQueue1* TaskQueue1::tq = nullptr;
std::mutex TaskQueue1::m_mutex;