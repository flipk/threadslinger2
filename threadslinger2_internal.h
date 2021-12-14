
#ifndef __T2T_INCLUDE_INTERNAL__

#error "This file is meant to be included only by threadslinger2.h"

#elif __T2T_INCLUDE_INTERNAL__ == 1

// hopefully we're already inside namespace ThreadSlinger2

//////////////////////////// SAFETY STUFF ////////////////////////////

#define __T2T_EVIL_CONSTRUCTORS(T)              \
    private:                                    \
    T(T&&) = delete;                            \
    T(const T&) = delete;                       \
    T &operator=(const T&) = delete;            \
    T &operator=(T&) = delete

#define __T2T_EVIL_NEW(T)                       \
    static void * operator new(size_t sz) = delete

#define __T2T_EVIL_DEFAULT_CONSTRUCTOR(T)       \
    private:                                    \
    T(void) = delete;

//////////////////////////// TRAITS STUFF ////////////////////////////

template <typename... Ts>
struct largest_type;

template <typename T>
struct largest_type<T>
{
    using type = T;
    static const int size = sizeof(type);
};

template <typename T, typename U, typename... Ts>
struct largest_type<T, U, Ts...>
{
    using type =
        typename largest_type<
        typename std::conditional<
            (sizeof(U) <= sizeof(T)), T, U
            >::type, Ts...
        >::type;
    static const int size = sizeof(type);
};

//////////////////////////// MY SHARED_PTR ////////////////////////////

template<class T>
t2t_shared_ptr<T> :: t2t_shared_ptr(T * _ptr /*= NULL*/)
    : ptr(NULL)
{
    ptr = _ptr;
    if (ptr)
        ptr->ref();
}

template<class T>
t2t_shared_ptr<T> :: t2t_shared_ptr(const t2t_shared_ptr<T> &other)
{
    ptr = other.ptr;
    if (ptr)
        ptr->ref();
}

template<class T>
t2t_shared_ptr<T> :: t2t_shared_ptr(t2t_shared_ptr<T> &&other)
{
    ptr = other.ptr;
    other.ptr = NULL;
}

template<class T>
t2t_shared_ptr<T> :: ~t2t_shared_ptr(void)
{
    if (ptr)
        ptr->deref();
}

template<class T>
void t2t_shared_ptr<T> :: set(T * _ptr)
{
    if (ptr)
        ptr->deref();
    ptr = _ptr;
    if (ptr)
        ptr->ref();
}

template<class T>
void t2t_shared_ptr<T> :: give(T * _ptr)
{
    if (ptr)
        ptr->deref();
    ptr = _ptr;
}

template<class T>
T * t2t_shared_ptr<T> :: take(void)
{
    T * ret = ptr;
    ptr = NULL;
    return ret;
}

template<class T>
template <class U>
bool t2t_shared_ptr<T> :: cast(const t2t_shared_ptr<U> &u)
{
    if (!u)
        return false;
    T * ret = dynamic_cast<T*>(*u);
    if (ret == NULL)
        return false;
    set(ret);
    return true;
}

template<class T>
t2t_shared_ptr<T> &
t2t_shared_ptr<T> :: operator=(const t2t_shared_ptr<T> &other)
{
    if (ptr)
        ptr->deref();
    ptr = other.ptr;
    if (ptr)
        ptr->ref();
    return *this;
}

template<class T>
t2t_shared_ptr<T> &
t2t_shared_ptr<T> :: operator=(t2t_shared_ptr<T> &&other)
{
    if (ptr)
        ptr->deref();
    ptr = other.ptr;
    other.ptr = NULL;
    return *this;
}

//////////////////////////// ERROR HANDLING ////////////////////////////

#define TS2_ASSERT(err,fatal) \
    ts2_assert_handler(err,fatal, __FILE__, __LINE__)

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

static_assert(std::is_trivial<__t2t_buffer_hdr>::value == true,
              "class __t2t_buffer_hdr must always be trivial");

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

#elif __T2T_INCLUDE_INTERNAL__ == 2

//////////////////////////// T2T_QUEUE ////////////////////////////

template <class BaseT>
t2t_queue<BaseT> :: t2t_queue(pthread_mutexattr_t *pmattr /*= NULL*/,
                              pthread_condattr_t  *pcattr /*= NULL*/)
    : q(pmattr,pcattr)
{
}

template <class BaseT>
void t2t_queue<BaseT> :: enqueue(BaseT * msg)
{
    __t2t_buffer_hdr * h = (__t2t_buffer_hdr *) msg;
    h--;
    h->ok();
    q._enqueue_tail(h);
}

template <class BaseT>
t2t_shared_ptr<BaseT>   t2t_queue<BaseT> :: dequeue(int wait_ms)
{
    t2t_shared_ptr<BaseT>  ret;
    __t2t_buffer_hdr * h = q._dequeue(wait_ms);
    if (h)
    {
        h++;
        ret.give((BaseT*) h);
    }
    return ret;
}

template <class BaseT>
// static class method
t2t_shared_ptr<BaseT> t2t_queue<BaseT> :: dequeue_multi(
    int num_queues,
    t2t_queue<BaseT> **queues,
    int *which_q,
    int wait_ms)
{
    t2t_shared_ptr<BaseT>  ret;
    __t2t_queue *qs[num_queues];
    for (int ind = 0; ind < num_queues; ind++)
        qs[ind] = &queues[ind]->q;
    __t2t_buffer_hdr * h = __t2t_queue::_dequeue_multi(
        num_queues, qs, which_q, wait_ms);
    if (h)
    {
        h++;
        ret.give((BaseT*) h);
    }
    return ret;
}

//////////////////////////// T2T_MESSAGE_BASE ////////////////////////////


template <class BaseT, class... DerivedTs>
template <class T, typename... ConstructorArgs>
// static class method
bool t2t_message_base<BaseT,DerivedTs...> :: get(
    T ** ptr, pool_t *pool, int wait_ms,
    ConstructorArgs&&... args)
{
    static_assert(std::is_base_of<BaseT, T>::value == true,
                  "allocated type must be derived from base type");
    static_assert(pool_t::buffer_size >= sizeof(T),
                  "allocated type must fit in pool buffer size, please "
                  "specify all message types in t2t_pool<> (and/or in "
                  "t2t_message_base<> if you're using ::pool_t)");

    T * t = new(pool,wait_ms,false)
        T(std::forward<ConstructorArgs>(args)...);
    *ptr = t;
    return (t != NULL);
}

template <class BaseT, class... DerivedTs>
template <class T, typename... ConstructorArgs>
// static class method
bool t2t_message_base<BaseT,DerivedTs...> :: get(
    T ** ptr, pool_t *pool,
    t2t_grow_flag grow,
    ConstructorArgs&&... args)
{
    static_assert(std::is_base_of<BaseT, T>::value == true,
                  "allocated type must be derived from base type");
    static_assert(pool_t::buffer_size >= sizeof(T),
                  "allocated type must fit in pool buffer size, please "
                  "specify all message types in t2t_pool<> (and/or in "
                  "t2t_message_base<> if you're using ::pool_t)");

    T * t = new(pool,0,true)
        T(std::forward<ConstructorArgs>(args)...);
    *ptr = t;
    return (t != NULL);
}

//////////////////////////////////////////////////////////////////

#endif /* __T2T2_INCLUDE_INTERNAL__ */
