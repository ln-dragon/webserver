#include "memory_pool.h"
#include <assert.h>

// template <typename T, size_t BlockSize>
MemoryPool::MemoryPool() {}

// template <typename T, size_t BlockSize>
MemoryPool::~MemoryPool() {
    Slot* cur = currentBolck_;
    while(cur) {
        Slot* next = cur->next;
        // free(reinterpret_cast<void *>(cur));
        // 转化为 void 指针，是因为 void 类型不需要调用析构函数,只释放空间
        operator delete(reinterpret_cast<void*>(cur));
        cur = next;
    }
}

// template <typename T, size_t BlockSize>
void MemoryPool::init(int size) {
    assert(size > 0);
    slotSize_ = size;
    currentBolck_ = NULL;
    currentSlot_ = NULL;
    lastSlot_ = NULL;
    freeSlot_ = NULL;
}

// 计算对齐所需补的空间
// template <typename T, size_t BlockSize>
inline size_t MemoryPool::padPointer(char* p, size_t align) {
    if (align == 0) {
        // 处理 align 为 0 的情况
        // 你可以选择抛出异常、记录错误，或者以一种安全的方式继续操作
        throw std::invalid_argument("Alignment value cannot be zero.");
    }
    size_t result = reinterpret_cast<size_t>(p);
    return ((align - result) % align);
}

// template <typename T, size_t BlockSize>
Slot* MemoryPool::allocateBlock() {
    char* newBlock = reinterpret_cast<char *>(operator new(BlockSize));//从操作系统中分配一块内存

    char* body = newBlock + sizeof(Slot*);//实际存储数据的起始位置
    // 计算为了对齐需要空出多少位置
    // size_t bodyPadding = padPointer(body, sizeof(slotSize_));
    size_t bodyPadding = padPointer(body, static_cast<size_t>(slotSize_));
    
    // 注意：多个线程（eventLoop共用一个MemoryPool）
    Slot* useSlot;
    {
        mutex_other_.lock();//修改锁
        // newBlock接到Block链表的头部
        reinterpret_cast<Slot *>(newBlock)->next = currentBolck_;
        currentBolck_ = reinterpret_cast<Slot *>(newBlock);
        // 为该Block开始的地方加上bodyPadding个char* 空间
        currentSlot_ = reinterpret_cast<Slot *>(body + bodyPadding);//当前内存槽指针
        //指向当前内存块中的最后一个内存槽
        lastSlot_ = reinterpret_cast<Slot *>(newBlock + BlockSize - slotSize_ + 1);
        useSlot = currentSlot_;

        // slot指针一次移动8个字节，移向下一个内存槽
        currentSlot_ += (slotSize_ >> 3);
        // currentSlot_ += slotSize_;
        mutex_other_.unlock(); // 手动解锁
    }

    return useSlot;
}

// template <typename T, size_t BlockSize>
//从内存池中获取一个内存槽的地址分配给用户
Slot* MemoryPool::nofree_solve() {
    if(currentSlot_ >= lastSlot_)
        return allocateBlock();
    Slot* useSlot;
    {
        mutex_other_.lock();//修改锁
        useSlot = currentSlot_;
        currentSlot_ += (slotSize_ >> 3);
        mutex_other_.unlock(); // 手动解锁
    }

    return useSlot;
}

//从freeslot链表中获取一个内存槽的地址
Slot* MemoryPool::allocate() {
    if(freeSlot_) {
        {
            mutex_freeSlot_.lock();//修改锁
            if(freeSlot_) {
                Slot* result = freeSlot_;
                freeSlot_ = freeSlot_->next;
                return result;
            }
            mutex_freeSlot_.unlock(); // 手动解锁
        }
    }
    //不能在链表中找到一个内存槽，则在内存池中申请一个新的内存槽
    return nofree_solve();
}

//已释放的内存槽添加到freeSlot_ 链表中
// template <typename T, size_t BlockSize>
inline void MemoryPool::deAllocate(Slot* p) {
    if(p) {
        // 将slot加入释放队列
        mutex_freeSlot_.lock();//修改锁
        p->next = freeSlot_;
        freeSlot_ = p;
        mutex_freeSlot_.unlock(); // 手动解锁
    }
}

//返回一个内存池对象
// template <typename T, size_t BlockSize>
MemoryPool& get_MemoryPool(int id) {
    static MemoryPool memorypool_[64];
    return memorypool_[id];//返回一个64个数组中的某一个内存池对象
}

// 初始化不同内存池对象中分别存放Slot大小为8，16，...，512字节的BLock链表
void init_MemoryPool() {
    for(int i = 0; i < 64; ++i) {
        get_MemoryPool(i).init((i + 1) << 3);
    }
}

// 超过512字节就直接new，没超过就从内存池中申请
void* use_Memory(size_t size) {
    if(!size)
        return nullptr;
    if(size > 512)
        return operator new(size);//从操作系统中申请内存
    
    // 相当于(size / 8)向上取整
    return reinterpret_cast<void *>(get_MemoryPool(((size + 7) >> 3) - 1).allocate());
}

void free_Memory(size_t size, void* p) {
    if(!p)  return;
    if(size > 512) {
        operator delete (p);//大于512字节就从操作系统中释放内存
        return;
    }
    get_MemoryPool(((size + 7) >> 3) - 1).deAllocate(reinterpret_cast<Slot *>(p));
}


