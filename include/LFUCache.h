#define LFU_CAPACITY 10
#include <string>
#include <unordered_map>
#include "lock.h"
#include "memory_pool.h"
using namespace std;

// 链表的节点
template<typename T>
class Node {
public:
 void setPre(Node* p) { pre_ = p; }
 void setNext(Node* p) { next_ = p; }
 Node* getPre() { return pre_; }
 Node* getNext() { return next_; }
 T& getValue() { return value_; }
private:
 T value_;
 Node* pre_;
 Node* next_;
};
// ⽂件名->⽂件内容的映射
struct Key {
    std::string key_, value_;
};
typedef Node<Key>* key_node;
// 链表：由多个Node组成
class KeyList {
public:
 void init(int freq);
 void destory();
 int getFreq();
 void add(key_node& node);
 void del(key_node& node);
 bool isEmpty();
 key_node getLast();
private:
 int freq_;
 key_node Dummyhead_;
 key_node tail_;
};
typedef Node<KeyList>* freq_node;
/*
典型的双重链表+hash表实现
⾸先是⼀个⼤链表，链表的每个节点都是⼀个⼩链表附带⼀个值表示频度
⼩链表存的是同⼀频度下的key value节点。
hash表存key到⼤链表节点的映射(key,freq_node)和
key到⼩链表节点的映射(key,key_node).
*/

// LFU由多个链表组成
class LFUCache {
private:
 freq_node Dummyhead_; // ⼤链表的头节点，⾥⾯每个节点都是⼩链表的头节点
 size_t capacity_;
 mutex mutex_;
 std::unordered_map<string, key_node> kmap_; // key到keynode的映射
 std::unordered_map<string, freq_node> fmap_; // key到freqnode的映射
 void addFreq(key_node& nowk, freq_node& nowf);
 void del(freq_node& node);
 void init();
public:
 static LFUCache& GetInstance();
//  LFUCache(int capicity);//有参构造
 LFUCache();//无参构造
 ~LFUCache();
 bool get(std::string& key, std::string& value); // 通过key返回value并进⾏LFU操作
 void set(std::string& key, std::string& value); // 更新LFU缓存
 size_t getCapacity() const { return capacity_; }
 void Initialize(int capacity);
};
