/** \file threadslinger2.h"
 * \brief describes all classes for ThreadSlinger 2.
 */

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

/** statistics for buffer pools */
struct t2t_pool_stats {
    int buffer_size;      //!< sizeof each buffer in the pool
    int total_buffers;    //!< current size of the pool
    int buffers_in_use;   //!< how many of those buffers in use
    int alloc_fails;      //!< how many times alloc/get returned null
    int grows;            //!< how many times pool has been grown
    int double_frees;     //!< how many times free buffer freed again
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

/** template for a user's pool; also see \ref t2t_message_base::pool_t
 *      and \ref t2t_message_base::get. 
 * \param T  the user's derived message class. */
template <class T>
class t2t_pool : public __t2t_pool
{
public:
    /** \brief constructor for a pool, also see \ref t2t_message_base::pool_t.
     * \param _num_bufs_init  how many buffers to put in pool initially.
     * \param _bufs_to_add_when_growing  if you use get(T2T_GROW) and it
     *        grows the pool, how many buffers should it add at a time.
     * \param pmattr  pthread mutex attributes; may pass NULL
     *                if you want defaults.
     * \param pcattr  pthread condition attributes; may pass NULL if you
     *                want defaults. take special note of
     *   pthread_condattr_setclock(pcattr, CLOCK_MONOTONIC).   */
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

/** template for a FIFO queue of messages.
 * \param T  the user's derived message class */
template <class T>
class t2t_queue
{
    __t2t_queue q;
public:
    /** constructor for a queue.
     *  a queue has a linked list, a mutex to protect updates to the list,
     *  and a pthread condition for blocking to sleep if the queue is empty
     *  and the user wants to wait to dequeue.
     * \param pmattr  pointer to a mutex attributes object to configure
     *                the mutex; NULL means accept pthread defaults.
     * \param pcattr  pointer to a condition attributes object to configure
     *                the condition; NULL means accept pthread defaults.
     *                take special note of
     *     pthread_condattr_setclock(pcattr, CLOCK_MONOTONIC).   */
    t2t_queue(pthread_mutexattr_t *pmattr = NULL,
              pthread_condattr_t *pcattr = NULL)
        : q(pmattr,pcattr) { }
    virtual ~t2t_queue(void) { }
    /** enqueue a message into this queue; the message must come
     *  from a pool of the same type. note the queue is a FIFO.
     * \param msg  pointer to a message to enqueue */
    void enqueue(T * msg)
    {
        __t2t_buffer_hdr * h = (__t2t_buffer_hdr *) msg;
        h--;
        h->ok();
        q._enqueue_tail(h);
    }
    /** dequeue a message from this queue in FIFO order.
     * \param wait_ms  how long to wait:
     *           <ul> <li> -1 : wait forever </li>
     *                <li> 0 : don't wait at all, if the pool is empty,
     *                     return NULL immediately. </li>
     *                <li> >0 : wait for some milliseconds for a buffer
     *                     to become available, then give up </li> </ul> */
    T * dequeue(int wait_ms)
    {
        __t2t_buffer_hdr * h = q._dequeue(wait_ms);
        if (h == NULL)
            return NULL;
        h++;
        return (T*) h;
    }
    /** watch multiple queues and dequeue from the first queue to have
     * a message waiting.
     * \param num_queues the size of the queues array to follow
     * \param queues an array of pointers to queues to check; note the
     *               order of this array implies priority: if several
     *               queues have messages waiting, you'll get the
     *               first message from the first queue first. it will
     *               only behave according to the wait_ms parameter if
     *               all specified queues are empty.
     * \param which_q if non-NULL, returns the index in the array
     *                specifying which queue the returned message came
     *                from. if we timed out and returned NULL, this
     *                will be written with -1. if this is NULL,
     *                nothing is written (assumes user didn't care
     *                which queue it came from).
     * \param wait_ms  how long to wait:
     *           <ul> <li> -1 : wait forever </li>
     *                <li> 0 : don't wait at all, if the pool is empty,
     *                     return NULL immediately. </li>
     *                <li> >0 : wait for some milliseconds for a buffer
     *                     to become available, then give up </li> </ul> */
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

/** a special flag to t2t_message_base::get telling it to grow the pool
 * if the pool is empty. */
enum class t2t_grow_flag {
    T2T_GROW = 1  //!<   a flag to t2t_message_base::get
};
#define T2T_GROW ThreadSlinger2::t2t_grow_flag::T2T_GROW

/** base class for all T2T messages. 
 *  to get a new message, user should call one of the two get()
 *  methods below. messages are constructed with
 *  a refcount of 1. if more than one thread needs to handle this
 *  message, each additional thread should call ref(), which will
 *  increase the refcount by 1.
 *
 *  users should not use delete; use deref() instead. each deref()
 *  decreases refcount by 1. if refcount hits 0, message is
 *  deleted.
 *
 * \param T   the derived data type's name.
 * \param ConstructorArgs  if your message has a constructor,
 *               you must list the constructor arg types here
 *               in the template. */
template <class T, typename... ConstructorArgs>
class t2t_message_base
{
public:
    /** a shortcut to \ref t2t_pool for the user to create a
     *  pool of these objects. */
    typedef t2t_pool<T> pool_t;
    /** a shortcut to \ref t2t_queue for the user to create a
     *  queue of these objects. */
    typedef t2t_queue<T> queue_t;

    // GROSS: the only way to make "delete" not-public is to make
    // "new" private, which requires class members to allocate stuff,
    // that's why we provide get() instead of letting users call
    // "new".

    /** get a new message from a pool and specify how long to wait.
     * the refcount on the returned message will be 1.
     * \param pool the pool_t to allocate from.
     * \param wait_ms  how long to wait:
     *           <ul> <li> -1 : wait forever </li>
     *                <li> 0 : don't wait at all, if the pool is empty,
     *                     return NULL immediately. </li>
     *                <li> >0 : wait for some milliseconds for a buffer
     *                     to become available, then give up </li> </ul>
     * \param args  if class T has a constructor, you must pass the args
     *              to the constructor here. */
    static T * get(pool_t *pool, int wait_ms,
                    ConstructorArgs&&... args)
    {
        return new(pool,wait_ms,false)
            T(std::forward<ConstructorArgs>(args)...);
    }

    /** get a new message from a pool and grow the pool if empty.
     * the refcount on the returned message will be 1.
     * if it needs to grow the pool, it will add the number
     * of buffers specified by _bufs_to_add_when_growing
     * parameter to \ref t2t_pool::t2t_pool.
     * \param pool the pool_t to allocate from.
     * \param grow  you must pass T2T_GROW here.
     * \param args  if class T has a constructor, you must pass the args
     *              to the constructor here. */
    static T * get(pool_t *pool, t2t_grow_flag grow,
                    ConstructorArgs&&... args)
    {
        return new(pool,0,true)
            T(std::forward<ConstructorArgs>(args)...);
    }

    /** increase reference count on this message. */
    void ref(void)
    {
        refcount++;
    }
    /** decrease reference count, if it hits zero,
     *  the message is returned to the pool. */
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
