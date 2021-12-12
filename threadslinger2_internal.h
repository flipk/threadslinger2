
#ifndef __T2T_INCLUDE_INTERNAL__
#error "This file is meant to be included only by threadslinger2.h"
#else

// hopefully we're already inside namespace ThreadSlinger2

//////////////////////////// T2T_LINKS ////////////////////////////

template <class T>
struct __t2t_links
{
    __t2t_links<T> * next;
    __t2t_links<T> * prev;
    __t2t_links<T> * list;
    static const uint32_t LINKS_MAGIC = 0x1e3909f2;
    uint32_t  magic;
    bool ok(void) const
    {
        if (magic == LINKS_MAGIC)
            return true;
        TS2_ASSERT(T2T_LINKS_MAGIC_CORRUPT,true);
        return false;
    }
    void init(void)
    {
        next = prev = this;
        list = NULL;
        magic = LINKS_MAGIC;
    }
    bool empty(void) const
    {
        return (ok() && (next == this) && (prev == this));
    }
    // a pool should be a stack to keep caches hot
    void add_head(T *item)
    {
        ok();
        if (item->list != NULL)
        {
            TS2_ASSERT(T2T_LINKS_ADD_ALREADY_ON_LIST,true);
        }
        item->next = next;
        item->prev = this;
        next->prev = item;
        next = item;
        item->list = this;
    }
    // a queue should be a fifo to keep msgs in order
    void add_tail(T *item)
    {
        ok();
        if (item->list != NULL)
        {
            TS2_ASSERT(T2T_LINKS_ADD_ALREADY_ON_LIST,true);
        }
        item->next = this;
        item->prev = prev;
        prev->next = item;
        prev = item;
        item->list = this;
    }
    bool validate(T *item)
    {
        return (ok() && (item->list == this));
    }
    void remove(void)
    {
        ok();
        if (list == NULL)
        {
            TS2_ASSERT(T2T_LINKS_REMOVE_NOT_ON_LIST,true);
        }
        list = NULL;
        next->prev = prev;
        prev->next = next;
        next = prev = this;
    }
    T * get_head(void)
    {
        ok();
        return (T*) next;
    }
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

//////////////////////////// T2T_TIMESPEC ////////////////////////////

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

//////////////////////////// T2T_QUEUE ////////////////////////////

class __t2t_queue
{
    pthread_mutex_t  mutex;
    pthread_cond_t   cond;
    clockid_t        clk_id;
    pthread_cond_t * waiting_cond;
    __t2t_buffer_hdr  buffers;
    void   lock(void) { pthread_mutex_lock  (&mutex); }
    void unlock(void) { pthread_mutex_unlock(&mutex); }
public:
    __t2t_queue(pthread_mutexattr_t *pmattr,
                pthread_condattr_t *pcattr);
    ~__t2t_queue(void);
    bool _validate(__t2t_buffer_hdr *h)
    {
        return buffers.validate(h);
    }
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

struct __t2t_container; // forward

/** base class for all t2t_pool template objects. */
class __t2t_pool
{
protected:
    t2t_pool_stats  stats;
    int bufs_to_add_when_growing;
    std::list<std::unique_ptr<__t2t_container>> container_pool;
    __t2t_queue q;
    __t2t_pool(int buffer_size,
               int _num_bufs_init,
               int _bufs_to_add_when_growing,
               pthread_mutexattr_t *pmattr,
               pthread_condattr_t *pcattr);
    virtual ~__t2t_pool(void);
public:
    /** add more buffers to this pool.
     * \param num_bufs  the number of buffers to add to the pool. */
    void add_bufs(int num_bufs);
    // if grow=true, ignore wait_ms; if grow=false,
    // -1 : wait forever, 0 : dont wait, >0 wait for some mS
    void * alloc(int wait_ms, bool grow = false);
    void release(void * ptr);
    /** retrieve statistics about this pool */
    void get_stats(t2t_pool_stats &_stats) const;

    __T2T_EVIL_CONSTRUCTORS(__t2t_pool);
    __T2T_EVIL_DEFAULT_CONSTRUCTOR(__t2t_pool);
};

//////////////////////////////////////////////////////////////////

#endif /* __T2T2_INCLUDE_INTERNAL__ */
