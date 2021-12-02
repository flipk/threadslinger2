
#ifndef __T2T_HEADER_FILE__
#define __T2T_HEADER_FILE__ 1

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <memory>
#include <vector>
#include <list>
#include <atomic>

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

#define __T2T_CHECK_COPYABLE(T) \
    static_assert(std::is_trivially_copyable<T>::value == 1, \
                  "class " #T " must always be trivially copyable")

namespace ThreadSlinger2 {

//////////////////////////// T2T_POOL_STATS ////////////////////////////

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

#define __T2T_INCLUDE_INTERNAL__ 1
#include "threadslinger2_internal.h"
#undef  __T2T_INCLUDE_INTERNAL__

//////////////////////////// T2T_POOL ////////////////////////////

template <class T>
class t2t_pool : public __t2t_pool
{
public:
    t2t_pool(int _num_bufs_init = 0,
             int _bufs_to_add_when_growing = 1,
             pthread_mutexattr_t *pmattr = NULL,
             pthread_condattr_t *pcattr = NULL)
        : __t2t_pool(sizeof(T), _num_bufs_init,
                     _bufs_to_add_when_growing,
                     pmattr, pcattr) { }
    virtual ~t2t_pool(void) { }

    __T2T_EVIL_CONSTRUCTORS(t2t_pool);
    __T2T_EVIL_DEFAULT_CONSTRUCTOR(t2t_pool);
};

//////////////////////////// T2T_QUEUE ////////////////////////////

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

//////////////////////////// T2T_MESSAGE_BASE ////////////////////////////

enum class t2t_grow_flag { T2T_GROW = 1 };
#define T2T_GROW ThreadSlinger2::t2t_grow_flag::T2T_GROW

// if your message has a constructor, you must list the
// constructor arg types here in the template.
template <class T, typename... ConstructorArgs>
class t2t_message_base
{
public:
    typedef t2t_pool<T> pool_t;
    typedef t2t_queue<T> queue_t;

    // if user needs an extra copy, user should call ref().  we dont
    // want users calling delete; we want them calling deref.  make()
    // gives it a refcount of 1 so a single deref() will delete it.
    // last deref() will call delete.  messages are constructed with a
    // refcount of 1.  the only way to make "delete" not-public is to
    // make "new" private, which requires class members to allocate
    // stuff.
    // wait_ms: -1 : wait forever, 0 : dont wait, >0 wait for mS
    static T * get(pool_t *pool, int wait_ms,
                    ConstructorArgs&&... args)
    {
        return new(pool,wait_ms,false)
            T(std::forward<ConstructorArgs>(args)...);
    }
    static T * get(pool_t *pool, t2t_grow_flag grow,
                    ConstructorArgs&&... args)
    {
        return new(pool,0,true)
            T(std::forward<ConstructorArgs>(args)...);
    }
    void ref(void)
    {
        refcount++;
    }
    void deref(void)
    {
        int v = refcount--;
        if (v <= 1)
            delete this;
    }

protected:
    t2t_message_base(void) : refcount(1) { }
    virtual ~t2t_message_base(void) { }
    // user should not use this, use deref() instead.
    static void operator delete(void *ptr)
    {
        T * obj = (T*) ptr;
        obj->__t2t_pool->release(ptr);
    }

private:
    pool_t * __t2t_pool;
    std::atomic_int  refcount;
    // this is a 'no-throw' version of operator new,
    // which returns NULL instead of throwing.
    // this is important, because it prevents calling
    // the constructor function on a NULL.
    // you MUST check the return of this for NULL.
    static void * operator new(size_t ignore_sz,
                               pool_t *pool,
                               int wait_ms, bool grow) throw ()
    {
        T * obj = (T*) pool->alloc(wait_ms, grow);
        if (obj == NULL)
            return NULL;
        obj->__t2t_pool = pool;
        return obj;
    }

    __T2T_EVIL_CONSTRUCTORS(t2t_message_base);
    __T2T_EVIL_NEW(t2t_message_base);
};

}; // namespace ThreadSlinger2

///////////////////////// STREAM OPS /////////////////////////

// this has to be outside the namespace
std::ostream &operator<<(std::ostream &strm,
                         const ThreadSlinger2::t2t_pool_stats &stats);

#endif /* __T2T_HEADER_FILE__ */
