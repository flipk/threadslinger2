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
#include <type_traits>

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

/** ThreadSlinger2 namespace, encapsulates all data structures for this API
 */
namespace ThreadSlinger2 {

//////////////////////////// error handler ////////////////////////////

/** the list of error types passed to an
 * ts2_assert_handler_t assertion handler. */
enum ts2_error_t {
    BUFFER_SIZE_TOO_BIG_FOR_POOL          = 1, //!< buffer size too big
    T2T_LINKS_MAGIC_CORRUPT               = 2, //!< magic sig corrupt
    T2T_LINKS_ADD_ALREADY_ON_LIST         = 3, //!< already on a list
    T2T_LINKS_REMOVE_NOT_ON_LIST          = 4, //!< not on any list
    T2T_POOL_RELEASE_ALREADY_ON_LIST      = 5, //!< already released
    DOUBLE_FREE                           = 6, //!< double free !

    // THIS API DOES NOT SUPPORT MORE THAN ONE THREAD
    // DEQUEUING FROM THE SAME QUEUE
    // AT THE SAME TIME SO DONT DO THAT
    T2T_QUEUE_MULTIPLE_THREAD_DEQUEUE     = 7, //!< multiple threads bad
    T2T_QUEUE_DEQUEUE_NOT_ON_THIS_LIST    = 8, //!< not on this list
    T2T_QUEUE_ENQUEUE_ALREADY_ON_A_LIST   = 9  //!< already on a list
};

/** defines the signature of any user-supplied assertion handler. */
typedef void (*ts2_assert_handler_t)(ts2_error_t e, bool fatal,
                                     const char *filename,
                                     int lineno);

/** a global variable holds a pointer to an assertion handler function. */
extern ts2_assert_handler_t ts2_assert_handler;

#define TS2_ASSERT(err,fatal) \
    ts2_assert_handler(err,fatal, __FILE__, __LINE__)

//////////////////////////// traits utility ////////////////////////////

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
template <class... T>
class t2t_pool : public __t2t_pool
{
public:
    static const int buffer_size = largest_type<T...>::size;
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
        : __t2t_pool(buffer_size,
                     _num_bufs_init,
                     _bufs_to_add_when_growing,
                     pmattr, pcattr) { }
    virtual ~t2t_pool(void) { }

    __T2T_EVIL_CONSTRUCTORS(t2t_pool);
    __T2T_EVIL_DEFAULT_CONSTRUCTOR(t2t_pool);
};

//////////////////////////// T2T_QUEUE ////////////////////////////

/** template for a FIFO queue of messages.
 * \param BaseT  the user's base message class */
template <class BaseT>
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
    void enqueue(BaseT * msg)
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
    BaseT * dequeue(int wait_ms)
    {
        __t2t_buffer_hdr * h = q._dequeue(wait_ms);
        if (h == NULL)
            return NULL;
        h++;
        return (BaseT*) h;
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
    static BaseT * dequeue_multi(int num_queues,
                             t2t_queue<BaseT> **queues,
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
        return (BaseT*) h;
    }

    __T2T_EVIL_CONSTRUCTORS(t2t_queue<BaseT>);
    __T2T_EVIL_NEW(t2t_queue<BaseT>);
    __T2T_EVIL_DEFAULT_CONSTRUCTOR(t2t_queue<BaseT>);
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
 * \param BaseT      the derived data type's base class name.
 * \param DerivedTs  if you want multiple possible messages, then you
 *     need to ensure the pool buffers are big enough for
 *     the largest of your message types; list all message
 *     types here. NOTE they should all be derived from
 *     BaseT and you should have a way in BaseT to figure
 *     out how to cast to the correct message type (i.e.
 *     make destructor virtual (making the type polymorphic)
 *     and then using dynamic_cast<>() (checking return type)). */
template <class BaseT, class... DerivedTs>
class t2t_message_base
{
public:
    /** a shortcut to \ref t2t_pool for the user to create a
     *  pool of these objects. */
    typedef t2t_pool<BaseT,DerivedTs...> pool_t;
    /** a shortcut to \ref t2t_queue for the user to create a
     *  queue of these objects. */
    typedef t2t_queue<BaseT> queue_t;

    // GROSS: the only way to make "delete" not-public is to make
    // "new" private, which requires class members to allocate stuff,
    // that's why we provide get() instead of letting users call
    // "new".

    /** get a new message from a pool and specify how long to wait.
     * the refcount on the returned message will be 1.
     * \return  true if allocation succeeded, false, if pool empty.
     * \param ptr   pointer to new object returned here.
     * \param pool the pool_t to allocate from.
     * \param wait_ms  how long to wait:
     *           <ul> <li> -1 : wait forever </li>
     *                <li> 0 : don't wait at all, if the pool is empty,
     *                     return NULL immediately. </li>
     *                <li> >0 : wait for some milliseconds for a buffer
     *                     to become available, then give up </li> </ul>
     * \param args  if class T has a constructor, you must pass the args
     *              to the constructor here. */
    template <class T, typename... ConstructorArgs>
    static bool get(T ** ptr, pool_t *pool, int wait_ms,
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

    /** get a new message from a pool and grow the pool if empty.
     * the refcount on the returned message will be 1.
     * if it needs to grow the pool, it will add the number
     * of buffers specified by _bufs_to_add_when_growing
     * parameter to \ref t2t_pool::t2t_pool.
     * \return  true if allocation succeeded, false, if pool empty.
     * \param ptr   pointer to new object returned here.
     * \param pool  the pool_t to allocate from.
     * \param grow  you must pass T2T_GROW here.
     * \param args  if class T has a constructor, you must pass the args
     *              to the constructor here. */
    template <class T, typename... ConstructorArgs>
    static bool get(T ** ptr, pool_t *pool,
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

    /** increase reference count on this message. when you invoke
     * \ref deref(), the reference count is decreased; if the refcount
     * hits zero, the object is deleted and returned to the pool it
     * came from. */
    void ref(void)
    {
        refcount++;
    }
    /** decrease reference count, if it hits zero,
     *  the message is returned to the pool. note you can call
     *  \ref ref() if you want to increase the refcount, if needed. */
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
        BaseT * obj = (BaseT*) ptr;
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
    static void * operator new(size_t wanted_sz,
                               pool_t *pool,
                               int wait_ms, bool grow) throw ()
    {
        if (wanted_sz > pool_t::buffer_size)
        {
            TS2_ASSERT(BUFFER_SIZE_TOO_BIG_FOR_POOL, false);
            return NULL;
        }
        BaseT * obj = (BaseT*) pool->alloc(wait_ms, grow);
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

//namepsace ThreadSlinger2 {

/** \mainpage  Threadslinger 2: message pools and queues

Overview

Classes of interest provided by this library:
 <ul>
 <li> \ref ThreadSlinger2::t2t_message_base
 <li> \ref ThreadSlinger2::t2t_pool
    <ul>
    <li> \ref ThreadSlinger2::t2t_pool_stats
    </ul>
 <li> \ref ThreadSlinger2::t2t_queue
 <li> \ref ThreadSlinger2::ts2_assert_handler
   <ul>
   <li> \ref ThreadSlinger2::ts2_error_t
   </ul>
 </ul>

Suppose you had a three messages you wanted to pass in a system, a base
class messages and two derived classes. Define them as below, deriving the
first class from ThreadSlinger2::t2t_message_base. Please note it is
important to list all derived types in the t2t_message_base template
argument list, so the code knows how big the buffers in the pool need
to be (for whatever is the largest of the derived types).

\code
class my_message_derived1; // forward
class my_message_derived2; // forward
class my_message_base
    : public ThreadSlinger2::t2t_message_base<
                 my_message_base,
                 my_message_derived1, my_message_derived2>
{
public:
    my_message_base(  constructor_args  );
    virtual ~my_message_base( void );
    // your data, and any other methods (including virtual!) go here
};

class my_message_derived1 : public my_message_base
{
public:
    my_message_derived1(  constructor_args  );
    ~my_message_derived1( void );
    // your data, and any other methods go here
};

class my_message_derived2 : public my_message_base
{
public:
    my_message_derived2(  constructor_args  );
    ~my_message_derived2( void );
    // your data, and any other methods go here
};
\endcode


Note that ThreadSlinger2::t2t_message_base automatically defines two
convienient data types to help define pools and queues.

\code
template <class BaseT, class... DerivedTs>
class t2t_message_base
{
public:
    typedef t2t_pool<BaseT,DerivedTs...> pool_t;
    typedef t2t_queue<BaseT> queue_t;

[ ... etc ... ]
};
\endcode

Next, use those convenience types to declare a pool which can contain
a set of buffers, and a queue for passing messages from one thread to
another.

\code
    pthread_mutexattr_t  mattr;
    pthread_condattr_t   cattr;
    pthread_mutexattr_init(&mattr);
    pthread_condattr_init(&cattr);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);

    // create a pool of my_message_base plus its derived types,
    // then pre-populate that pool with 2 buffers.
    // each time "get" is called with T2T_GROW and the pool is
    // empty, add 10 more buffers.  When we have to lock a mutex
    // and when we have to sleep on a condition for more buffers,
    // use the supplied mutex and cond attributes, or pass NULL
    // for defaults.
    my_message_base::pool_t   mypool(2,10,&mattr,&cattr);

    // create a queue which can pass my_message_base and it's
    // derived types.
    my_message_base::queue_t    q(&mattr,&cattr);

    pthread_mutexattr_destroy(&mattr);
    pthread_condattr_destroy(&cattr);
\endcode

Now, a sender may allocate new buffers from the pool, and any arguments
required for the object constructors are passed through the arguments
to the \ref ThreadSlinger2::t2t_message_base::get call.

Once it has allocated a buffer and filled it out, it enqueues it using
\ref ThreadSlinger2::t2t_queue::enqueue.

\code
    my_message_base * mb;

    if (my_message_base::get(&mb, &mypool,
               <time to wait>,
               <args to my_message_base constructor>))
    {
        printf("enqueuing a message NOW\n");
        q.enqueue(mb);
    }
    else
        printf("failed to allocate a buffer!\n");
\endcode

A sender may also allocate a derived message type too, using the same
syntax; note if the type specified is not actually a base type, or if
you didn't specify the derived class type in the t2t_message_base
parameter list and the buffers in the pool are too small for this type,
you will get compile-time errors telling you this.

Also note, this example shows you how to specify that the pool should
grow if it is currently empty.

\code
    my_message_d2 * md2;

    // NOTE using my_message_b:: instead of my_message_d2:: !
    if (my_message_base::get(&md2,&mypool,
               T2T_GROW,
               <args for my_message_d2 constructor>))
    {
        printf("enqueuing a message NOW\n");
        q.enqueue(md2);
    }
\endcode

The recipient thread then dequeues from this queue using
\ref ThreadSlinger2::t2t_queue::dequeue  (or
\ref ThreadSlinger2::t2t_queue::dequeue_multi)
to process the
messages in its own time. Whenever the reader is done with a message
and no longer wants to keep the pointer, it should call
\ref ThreadSlinger2::t2t_message_base::deref
on it.  This will invoke the object's destructor method and then
release the buffer back to the pool it came from.

\code

    while (keep_running)
    {
        my_message_base * mb = NULL;
        my_message_derived1 * md1 = NULL;
        my_message_derived2 * md2 = NULL;

        // wait for up to 1000 mS for a message
        mb = q.dequeue(1000);
        if (mb)
        {
            printf("READER GOT MSG\n");

            md1 = dynamic_cast<my_message_derived1*>(mb);
            if (md1)
                printf("message is of type my_message_derived1!\n");

            md2 = dynamic_cast<my_message_derived2*>(mb);
            if (md2)
                printf("message is of type my_message_derived2!\n");

            mb->deref();
        }
        else
            printf("READER GOT NULL\n");
    }

\endcode

Finally, by default errors are caught and printed on stderr; fatal errors
cause an exit of the process. If you want your own handler for ThreadSlinger2
errors, you can register a function to handle them using the global variable
\ref ThreadSlinger2::ts2_assert_handler :

\code

static void
custom_ts2_assert_handler(ThreadSlinger2::ts2_error_t e,
                          bool fatal,
                          const char *filename,
                          int lineno)
{
    fprintf(stderr, "\n\nERROR: ThreadSlinger2 ASSERTION %d at %s:%d\n\n",
            filename, lineno);
    if (fatal)
        exit(1);
}

void register_new_assertion_handler(void)
{
   ThreadSlinger2::ts2_assert_handler = &default_ts2_assert_handler;
}

\endcode


 */

//}; // namespace ThreadSlinger2
