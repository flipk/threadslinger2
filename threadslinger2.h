
#include "dll3.h"
#include <pthread.h>
#include <stdlib.h>
#include <memory>
#include <vector>
#include <list>
#include <atomic>

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

#define __T2T_CHECK_COPYABLE(T) \
    static_assert(std::is_trivially_copyable<T>::value == 1, \
                  "class " #T " must always be trivially copyable")

//////////////////////////// T2T_CONTAINER ////////////////////////////

struct __t2t_container
{
    typedef std::unique_ptr<__t2t_container> up;
    // this is required by unique_ptr, because it has
    // a static_assert(sizeof(_Tp)>0) in it.....
    int dummy;
    uint64_t data[0]; // forces entire struct to 8 byte alignment
    __t2t_container(void)  { dummy = 0; }
    void *operator new(size_t ignore_sz, int real_size)
    { return malloc(real_size + sizeof(__t2t_container)); }
    void  operator delete(void *ptr)
    { free(ptr); }
    __T2T_EVIL_CONSTRUCTORS(__t2t_container);
    __T2T_EVIL_NEW(__t2t_container);
};

__T2T_CHECK_COPYABLE(__t2t_container);

//////////////////////////// T2T_LINKS ////////////////////////////

template <class T>
struct __t2t_links
{
    __t2t_links<T> * __t2t_next;
    __t2t_links<T> * __t2t_prev;
    __t2t_links<T> * __t2t_list;
    static const uint32_t LINKS_MAGIC = 0x1e3909f2;
    uint32_t  magic;
    bool ok(void) const
    {
        if (magic == LINKS_MAGIC)
            return true;
        printf("T2T LINKS CORRUPT SOMEHOW\n");
        exit(1);
    }
    void init(void)
    {
        __t2t_next = __t2t_prev = this;
        __t2t_list = NULL;
        magic = LINKS_MAGIC;
    }
    bool empty(void) const
    {
        return (ok() && (__t2t_next == this) && (__t2t_prev == this));
    }
    // a pool should be a stack to keep caches hot
    void add(T *item)
    {
        ok();
        item->__t2t_next = __t2t_next;
        item->__t2t_prev = this;
        __t2t_next->__t2t_prev = item;
        __t2t_next = item;
        item->__t2t_list = this;
    }
    // a queue should be a fifo to keep msgs in order
    void add_tail(T *item)
    {
        ok();
        item->__t2t_next = this;
        item->__t2t_prev = __t2t_prev;
        __t2t_prev->__t2t_next = item;
        __t2t_prev = item;
        item->__t2t_list = this;
    }
    bool validate(T *item)
    {
        return (ok() && (item->__t2t_list == this));
    }
    void remove(void)
    {
        ok();
        __t2t_list = NULL;
        __t2t_next->__t2t_prev = __t2t_prev;
        __t2t_prev->__t2t_next = __t2t_next;
        __t2t_next = __t2t_prev = this;
    }
    T * __t2t_get_next(void) { ok(); return (T*) __t2t_next; }
    T * __t2t_get_prev(void) { ok(); return (T*) __t2t_prev; }
};

//////////////////////////// T2T_BUFFER_HDR ////////////////////////////

struct __t2t_buffer_hdr : public __t2t_links<__t2t_buffer_hdr>
{
    bool inuse;
    uint64_t data[0]; // forces entire struct to 8 byte alignment
    void init(void)
    {
        __t2t_links::init();
        inuse = false;
    }
};

__T2T_CHECK_COPYABLE(__t2t_buffer_hdr);

//////////////////////////// T2T_QUEUE ////////////////////////////

struct __t2t_timespec : public timespec
{
    __t2t_timespec(void) { }
    __t2t_timespec(int ms)
    {
        tv_sec = ms / 1000;
        tv_nsec = (ms % 1000) * 1000;
    }
    void getNow(clockid_t clk_id)
    {
        clock_gettime(clk_id, this);
    }
    __t2t_timespec &operator+=(const timespec &rhs)
    {
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
    pthread_cond_t * waiting_cond;
    void   lock(void) { pthread_mutex_lock  (&mutex); }
    void unlock(void) { pthread_mutex_unlock(&mutex); }

    __t2t_links<__t2t_buffer_hdr>  buffers;
public:
    __t2t_queue(pthread_mutexattr_t *pmattr,
                pthread_condattr_t *pcattr);
    ~__t2t_queue(void);
    bool _validate(__t2t_buffer_hdr *h)
    { return buffers.validate(h); }
    // -1 : wait forever
    //  0 : dont wait, just return
    // >0 : wait for some number of mS
    __t2t_buffer_hdr *_dequeue(int wait_ms);
    static __t2t_buffer_hdr *_dequeue_multi(
        int num_queues,
        __t2t_queue **queues,
        int *which_queue,
        int wait_ms);
    // a pool should be a stack, to keep caches hotter.
    void _enqueue(__t2t_buffer_hdr *h);
    // a queue should be a fifo, to keep msgs in order.
    void _enqueue_tail(__t2t_buffer_hdr *h);

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
    void init(int _buffer_size) {
        buffer_size = _buffer_size;
        total_buffers = 0;
        buffers_in_use = 0;
        alloc_fails = 0;
        grows = 0;
        double_frees = 0;
    }
    t2t_pool_stats(int _buffer_size = 0) { init(_buffer_size); }
};

__T2T_CHECK_COPYABLE(t2t_pool_stats);

template <class T>
class t2t_pool
{
    t2t_pool_stats  stats;
    int bufs_to_add_when_growing;
    std::list<__t2t_container::up> container_pool;
    __t2t_queue q;
public:
    t2t_pool(int _num_bufs_init = 0,
             int _bufs_to_add_when_growing = 1,
             pthread_mutexattr_t *pmattr = NULL,
             pthread_condattr_t *pcattr = NULL)
        : stats(sizeof(T)), q(pmattr, pcattr)
    {
        bufs_to_add_when_growing = _bufs_to_add_when_growing;
        add_bufs(_num_bufs_init);
    }
    virtual ~t2t_pool(void) { }
    void add_bufs(int num_bufs)
    {
        if (num_bufs <= 0)
            return;
        int real_buffer_size = stats.buffer_size + sizeof(__t2t_buffer_hdr);
        int container_size = num_bufs * real_buffer_size;
        __t2t_container * c = new(container_size) __t2t_container;
        container_pool.push_back(__t2t_container::up(c));
        uint8_t * ptr = (uint8_t *) c->data;
        for (int ind = 0; ind < num_bufs; ind++)
        {
            __t2t_buffer_hdr * h = (__t2t_buffer_hdr *) ptr;
            h->init();
            stats.total_buffers ++;
            q._enqueue(h);
            ptr += real_buffer_size;
        }
    }
    // if grow=true, ignore wait_ms; if grow=false,
    // -1 : wait forever, 0 : dont wait, >0 wait for some mS
    void * alloc(int wait_ms, bool grow = false)
    {
        __t2t_buffer_hdr * h = NULL;
        if (grow)
        {
            h = q._dequeue(0);
            if (h == NULL)
            {
                add_bufs(bufs_to_add_when_growing);
                stats.grows ++;
                h = q._dequeue(0);
            }
        }
        else
        {
            h = q._dequeue(wait_ms);
        }
        if (h == NULL)
        {
            stats.alloc_fails ++;
            return NULL;
        }
        h->inuse = true;
        stats.buffers_in_use++;
        h++;
        return h;
    }
    void release(void * ptr)
    {
        __t2t_buffer_hdr * h = (__t2t_buffer_hdr *) ptr;
        h--;
        if (h->__t2t_list != NULL)
            printf("VALIDATION FAIL\n");
        if (h->inuse == false)
        {
            printf("DOUBLEFREE\n");
            stats.double_frees ++;
        }
        else
        {
            h->inuse = false;
            q._enqueue(h);
            stats.buffers_in_use --;
        }
    }
    void get_stats(t2t_pool_stats &_stats) const { _stats = stats; }

    __T2T_EVIL_CONSTRUCTORS(t2t_pool);
    __T2T_EVIL_DEFAULT_CONSTRUCTOR(t2t_pool);
};

//////////////////////////// T2T_MESSAGE_BASE ////////////////////////////

enum class t2t_grow_flag { T2T_GROW = 1 };
#define T2T_GROW ThreadSlinger2::t2t_grow_flag::T2T_GROW

template <class T> class t2t_queue; // forward

template <class T>
class t2t_message_base
{
public:
    typedef t2t_pool<T> pool_t;
    typedef t2t_queue<T> queue_t;
    typedef std::shared_ptr<T> sp;
private:
    pool_t * __t2t_pool;
    std::atomic_int  x;
protected:
    t2t_message_base(void) : x(1) { }
public:
    virtual ~t2t_message_base(void) { }
    // this is a 'no-throw' version of operator new,
    // which returns NULL instead of throwing.
    // this is important, because it prevents calling
    // the constructor function on a NULL.
    // you MUST check the return of this for NULL.
    // wait_ms: -1 : wait forever, 0 : dont wait, >0 wait for mS
    static void * operator new(size_t ignore_sz,
                               pool_t *pool,
                               int wait_ms = -1) throw ()
    {
        T * obj = (T*) pool->alloc(wait_ms, false);
        if (obj == NULL)
            return NULL;
        obj->__t2t_pool = pool;
        return obj;
    }

    // message is constructed with a refcount of 1.
    static void * operator new(size_t ignore_sz,
                               pool_t *pool,
                               t2t_grow_flag grow) throw ()
    {
        T * obj = (T*) pool->alloc(0, true);
        if (obj == NULL)
            return NULL;
        obj->__t2t_pool = pool;
        return obj;
    }

    // user should not use this, use deref() instead.
    static void operator delete(void *ptr)
    {
        T * obj = (T*) ptr;
        obj->__t2t_pool->release(ptr);
    }

    // if user needs an extra copy, user should call ref()
    void ref(void) { x++; }

    // user should call this instead of deleting;
    // last deref() will call delete above.
    void deref(void)
    {
        int v = x.fetch_sub(1);
        if (v == 1)
            delete this;
    }

    __T2T_EVIL_CONSTRUCTORS(t2t_message_base);
    __T2T_EVIL_NEW(t2t_message_base);
};

template <class T>
class t2t_queue
{
    __t2t_queue q;
public:
    t2t_queue(pthread_mutexattr_t *pmattr = NULL,
              pthread_condattr_t *pcattr = NULL)
        : q(pmattr,pcattr) { }
    virtual ~t2t_queue(void) { }
    void enqueue(T * msg)
    {
        __t2t_buffer_hdr * h = (__t2t_buffer_hdr *) msg;
        h--;
        h->ok();
        q._enqueue_tail(h);
    }
    T * dequeue(int wait_ms)
    {
        __t2t_buffer_hdr * h = q._dequeue(wait_ms);
        if (h == NULL)
            return NULL;
        h++;
        return (T*) h;
    }
    static T * dequeue_multi(int num_queues,
                             t2t_queue<T> **queues,
                             int *which_q,
                             int wait_ms)
    {
        __t2t_queue *qs[num_queues];
        for (int ind = 0; ind < num_queues; ind++)
            qs[ind] = &queues[ind]->q;
        __t2t_buffer_hdr * h = __t2t_queue::_dequeue_multi(
            num_queues, qs, which_q, wait_ms);
        if (h == NULL)
            return NULL;
        h++;
        return (T*) h;
    }

    __T2T_EVIL_CONSTRUCTORS(t2t_queue<T>);
    __T2T_EVIL_NEW(t2t_queue<T>);
    __T2T_EVIL_DEFAULT_CONSTRUCTOR(t2t_queue<T>);
};

}; // namespace ThreadSlinger2

#undef __T2T_EVIL_CONSTRUCTORS
#undef __T2T_EVIL_DEFAULT_CONSTRUCTOR
#undef __T2T_CHECK_COPYABLE

///////////////////////// STREAM OPS /////////////////////////

// this has to be outside the namespace
std::ostream &operator<<(std::ostream &strm,
                         const ThreadSlinger2::t2t_pool_stats &stats);
