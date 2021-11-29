
// Buffer pools of bytes, ‘new’ and ‘delete’ pull buffers from pools
// and use constructor.  Integrate with std::shared_ptr?  Queue is a
// list of shared_ptrs, and dequeue is a move? So you can just loose
// the last reference, shared_ptr invokes delete, and it gets returned
// to the pool.  Can you do a ‘new’ with args, so you can make it
// return NULL if timeout exceeded?

#include "dll3.h"
#include <pthread.h>
#include <stdlib.h>
#include <memory>
#include <vector>
#include <list>

namespace ThreadSlinger2 {

#define __T2T_EVIL_CONSTRUCTORS(T)              \
private:                                        \
    T(T&&) = delete;                            \
    T(const T&) = delete;                       \
    T &operator=(const T&) = delete;            \
    T &operator=(T&) = delete

#define __T2T_EVIL_NEW(T)       \
    static void * operator new(size_t sz) = delete

#define __T2T_EVIL_DEFAULT_CONSTRUCTOR(T)       \
private:                                        \
    T(void) = delete;

//////////////////////////// T2T_LINKS ////////////////////////////

template <class T>
struct __t2t_links
{
    __t2t_links<T> * __t2t_next;
    __t2t_links<T> * __t2t_prev;
    __t2t_links<T> * __t2t_list;
    void init(void)
    { __t2t_next = __t2t_prev = this; __t2t_list = NULL; }
    bool empty(void) const
    { return ((__t2t_next == this) && (__t2t_prev == this)); }
    void add(T *item)
    {
        item->__t2t_next = __t2t_next;
        item->__t2t_prev = this;
        __t2t_next->__t2t_prev = item;
        __t2t_next = item;
        item->__t2t_list = this;
    }
    bool validate(T *item)
    {
        return (item->__t2t_list == this);
    }
    void remove(void)
    {
        __t2t_list = NULL;
        __t2t_next->__t2t_prev = __t2t_prev;
        __t2t_prev->__t2t_next = __t2t_next;
        __t2t_next = __t2t_prev = this;
    }
    T * __t2t_get_next(void) { return (T*) __t2t_next; }
    T * __t2t_get_prev(void) { return (T*) __t2t_prev; }
};

//////////////////////////// T2T_BUFFER_HDR ////////////////////////////

struct __t2t_buffer_hdr : public __t2t_links<__t2t_buffer_hdr>
{
    int buffer_size;
    int container_size;
    bool inuse;
    uint64_t data[0]; // forces entire struct to 8 byte alignment
    void init(int _bufsiz, int _contsize)
    {
        __t2t_links::init();
        buffer_size = _bufsiz;
        container_size = _contsize;
        inuse = false;
    }
};

//////////////////////////// T2T_CONTAINER ////////////////////////////

struct __t2t_container
{
    typedef std::unique_ptr<__t2t_container> up;

    int container_size;
    uint64_t data[0]; // forces entire struct to 8 byte alignment
    __t2t_container(int _container_size);
    void *operator new(size_t ignore_sz, int real_size);
    void  operator delete(void *ptr);

    __T2T_EVIL_CONSTRUCTORS(__t2t_container);
    __T2T_EVIL_NEW(__t2t_container);
    __T2T_EVIL_DEFAULT_CONSTRUCTOR(__t2t_container);
};

//////////////////////////// T2T_QUEUE ////////////////////////////

struct __t2t_timespec : public timespec
{
    __t2t_timespec(void) { /*nothing*/ }
    __t2t_timespec(int ms)
    {
        tv_sec = ms / 1000;
        tv_nsec = (ms % 1000) * 1000;
    }
    void getNow(clockid_t clk_id)
    {
        clock_gettime(clk_id, this);
    }
    __t2t_timespec &operator+=(const timespec &rhs) {
        tv_sec += rhs.tv_sec;
        tv_nsec += rhs.tv_nsec;
        if (tv_nsec > 1000000000)
        {
            tv_nsec -= 1000000000;
            tv_sec += 1;
        }
        return *this;
    }
};

class __t2t_queue
{
    pthread_mutex_t  mutex;
    pthread_cond_t   cond;
    clockid_t        clk_id;
    bool waiting;
    void   lock(void) { pthread_mutex_lock  (&mutex); }
    void unlock(void) { pthread_mutex_unlock(&mutex); }

    __t2t_links<__t2t_buffer_hdr>  buffers;
public:
    __t2t_queue(pthread_mutexattr_t *pmattr,
                pthread_condattr_t *pcattr,
                clockid_t  _clk); // CLOCK_REALTIME, CLOCK_MONOTONIC
    ~__t2t_queue(void);
    bool _validate(__t2t_buffer_hdr *h)
    { return buffers.validate(h); }
    // -1 : wait forever
    //  0 : dont wait, just return
    // >0 : wait for some number of mS
    __t2t_buffer_hdr *_dequeue(int wait_ms);
    void _enqueue(__t2t_buffer_hdr *h);

    __T2T_EVIL_CONSTRUCTORS(__t2t_queue);
    __T2T_EVIL_NEW(__t2t_queue);
    __T2T_EVIL_DEFAULT_CONSTRUCTOR(__t2t_queue);
};

//////////////////////////// T2T_POOL ////////////////////////////

struct t2t_pool_stats {
    int buffer_size;
    int total_buffers;
    int buffers_in_use;
    int alloc_fails;
    int grows;
    int double_frees;
    void init(void) {
        buffer_size = 0;
        total_buffers = 0;
        buffers_in_use = 0;
        alloc_fails = 0;
        grows = 0;
        double_frees = 0;
    }
    t2t_pool_stats(void) { init(); }
};

class __t2t_pool
{
private:
    t2t_pool_stats  stats;
    int bufs_to_add_when_growing;
    std::list<__t2t_container::up> container_pool;
    __t2t_queue q;
public:
    __t2t_pool(int _buffer_size,
               int _num_bufs_init,
               int _bufs_to_add_when_growing,
               pthread_mutexattr_t *pmattr,
               pthread_condattr_t *pcattr,
               clockid_t  _clk);
    ~__t2t_pool(void);
    void _add_bufs(int num_bufs);
    // if grow=true, ignore wait_ms; if grow=false,
    // -1 : wait forever, 0 : dont wait, >0 wait for some mS
    __t2t_buffer_hdr *_alloc(int wait_ms, bool grow = false);
    bool _validate(__t2t_buffer_hdr *h)
    { return q._validate(h); }
    void _release(__t2t_buffer_hdr *buf);
    void get_stats(t2t_pool_stats &_stats) const { _stats = stats; }

    __T2T_EVIL_CONSTRUCTORS(__t2t_pool);
    __T2T_EVIL_DEFAULT_CONSTRUCTOR(__t2t_pool);
};

template <class T>
struct t2t_pool : public __t2t_pool
{
public:
    t2t_pool(int _num_bufs_init = 0,
             int _bufs_to_add_when_growing = 1,
             pthread_mutexattr_t *pmattr = NULL,
             pthread_condattr_t *pcattr = NULL,
             clockid_t  _clk = CLOCK_REALTIME)
        : __t2t_pool(sizeof(T), _num_bufs_init,
                     _bufs_to_add_when_growing,
                     pmattr, pcattr, _clk) { }
    ~t2t_pool(void) { }
    void add_bufs(int num_bufs)
    {
        _add_bufs(num_bufs);
    }
    void * alloc(int wait_ms, bool grow = false)
    {
        __t2t_buffer_hdr * h = _alloc(wait_ms, grow);
        if (h == NULL)
            return NULL;
        h++;
        return h;
    }
    void release(void * ptr)
    {
        __t2t_buffer_hdr * h = (__t2t_buffer_hdr *) ptr;
        h--;
        if (h->__t2t_list != NULL)
            printf("VALIDATION FAIL\n");
        _release(h);
    }

    __T2T_EVIL_CONSTRUCTORS(t2t_pool);
    __T2T_EVIL_DEFAULT_CONSTRUCTOR(t2t_pool);
};

//////////////////////////// T2T_MESSAGE_BASE ////////////////////////////

template <class T>
class t2t_message_base
{
public:
    typedef t2t_pool<T> pool_t;
    typedef std::shared_ptr<T> sp;
    typedef std::unique_ptr<T> up;
private:
    pool_t * __pool;
protected:
    t2t_message_base(void) { /*nothing?*/ }
public:
    virtual ~t2t_message_base(void) { /*nothing?*/ }
    // this is a 'no-throw' version of operator new,
    // which returns NULL instead of throwing.
    // this is important, because it prevents calling
    // the constructor function on a NULL.
    // you MUST check the return of this for NULL.
    static void * operator new(size_t ignore_sz,
                               pool_t *pool,
                               int wait_ms,
                               bool grow) throw ()
    {
        T * obj = (T*) pool->alloc(wait_ms, grow);
        if (obj == NULL)
            return NULL;
        obj->__pool = pool;
        return obj;
    }
    static void operator delete(void *ptr)
    {
        T * obj = (T*) ptr;
        obj->__pool->release(ptr);
    }

    __T2T_EVIL_CONSTRUCTORS(t2t_message_base);
    __T2T_EVIL_NEW(t2t_message_base);
};


}; // namespace ThreadSlinger2

#undef __T2T_EVIL_CONSTRUCTORS
#undef __T2T_EVIL_DEFAULT_CONSTRUCTOR

///////////////////////// STREAM OPS /////////////////////////

// this has to be outside the namespace
std::ostream &operator<<(std::ostream &strm,
                         const ThreadSlinger2::t2t_pool_stats &stats);
