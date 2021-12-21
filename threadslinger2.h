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

/** ThreadSlinger2 namespace, encapsulates all data structures for this API
 */
namespace ThreadSlinger2 {

//////////////////////////// ERROR HANDLING ////////////////////////////

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
    T2T_QUEUE_ENQUEUE_ALREADY_ON_A_LIST   = 9, //!< already on a list
    T2T_QUEUE_SET_EMPTY                   = 10 //!< queue set empty
};

/** defines the signature of any user-supplied assertion handler
 * \note If your assert handler is passed fatal=true, your assert handler
 *   \em must not continue execution of this thread. To continue execution
 * is to invite SEGV or other errors. */
typedef void (*ts2_assert_handler_t)(ts2_error_t e, bool fatal,
                                     const char *filename,
                                     int lineno);

/** a global variable holds a pointer to an assertion handler function
 * \note If your assert handler is passed fatal=true, your assert handler
 *   \em must not continue execution of this thread. To continue execution
 * is to invite SEGV or other errors. */
extern ts2_assert_handler_t ts2_assert_handler;

//////////////////////////// T2T_POOL_STATS ////////////////////////////

/** statistics for buffer pools */
struct t2t_pool_stats {
    t2t_pool_stats(int _buffer_size = 0);
    void init(int _buffer_size);

    int buffer_size;      //!< sizeof each buffer in the pool
    int total_buffers;    //!< current size of the pool
    int buffers_in_use;   //!< how many of those buffers in use
    int alloc_fails;      //!< how many times alloc/get returned null
    int grows;            //!< how many times pool has been grown
    int double_frees;     //!< how many times free buffer freed again
};

//////////////////////////// MY SHARED_PTR ////////////////////////////

/** t2t version of std::shared_ptr, used by t2t_queue
 * \param T  the message class being shared.
 *
 * \note I recommend making an alias in every message class you
 *    create, like
 *    "typedef ThreadSlinger2::t2t_shared_ptr<classname> sp_t".
 *    See the example code.
 */
template <class T>
struct t2t_shared_ptr {
    /** constructor will ref() an obj passed to it, but can take NULL */
    t2t_shared_ptr<T>(T * _ptr = NULL);

    /** copy constructor will copy the pointer in the other object and
     * will ref() the pointer. */
    template <class BaseT>
    t2t_shared_ptr<T>(const t2t_shared_ptr<BaseT> &other);

    /** move constructor will remove the pointer from the source but
     * will not change the refcounter */
    t2t_shared_ptr<T>(t2t_shared_ptr<T> &&other);

    /** dereferences the contained pointer, if there is one, and if this
     * is the last reference, the object is destroyed / returned
     * to whatever pool it came from. */
    ~t2t_shared_ptr<T>(void);

    /** overwrite a ptr in this obj with a new
     * pointer, derefing the old ptr and refing
     * the new pointer in the process. */
    void reset(T * _ptr = NULL);

    /** access the pointer within, same as -> or * */
    T * get(void) const { return ptr; }

    /** give a pointer to this object without changing its refcount. */
    void give(T * _ptr);

    /** take a pointer out of this object
     * without changing its refcount; after this call,
     * this object now stores a NULL pointer (is empty). */
    T * take(void);

    /** just like std::shared_ptr, this returns the ref count
     * of the contained object.
     * \note if this shared ptr is currently empty, the return
     *     value is 0, which would otherwise be impossible.
     * \note this is only advisory! if this object is shared
     *     amongst threads, the value could change. the only
     *     value that's really useful is 1, meaning the calling
     *     thread has the only reference. */
    int use_count(void) const;

    /** just like std::shared_ptr, this returns true if the
     * ref count is exactly 1 (this is the only shared pointer
     * referencing the contained object). */
    bool unique(void) const;

    /** assign, takes another ref() on the object,
     * also does dynamic_cast.
     * \note this can take a pointer to a base class and will
     *    dynamic_cast to the derived type; assumes the types are
     *    polymorphic, and will set this object to NULL (empty) if
     *    the dynamic_cast says it's not actually this derived type. */
    template <class BaseT>
    t2t_shared_ptr<T> &operator=(const t2t_shared_ptr<BaseT> &other);

    /** move, useful for zero copy return */
    t2t_shared_ptr<T> &operator=(t2t_shared_ptr<T> &&other);

    /** useful for "if (sp)" to check if this obj has a pointer or not */
    operator bool() const { return (ptr != NULL); }

    /** access the pointer using "obj->" */
    T * operator->(void) const { return ptr; }

    /** access the pointer using "*obj" */
    T * operator*(void) const { return ptr; }

private:
    void ref(void);
    void deref(void);
    T * ptr;
};

//////////////////////////// T2T_WAIT_FLAG ////////////////////////////

/** wait interval values, for pool allocs; note GROW is
 * not relevant to dequeueing */
enum wait_flag
{
    /** -2 means grow pool if empty (not applicable to dequeue) */
    GROW = -2,
    WAIT_FOREVER = -1,   //!< -1 means wait forever for a free buffer
    NO_WAIT = 0,         //!<  0 means return NULL immediately if empty
    WAIT_ONE_SEC = 1000  //!< any value >0 is # of milliseconds to wait
};

#define __T2T_INCLUDE_INTERNAL__ 1
#include "threadslinger2_internal.h"
#undef  __T2T_INCLUDE_INTERNAL__

//////////////////////////// T2T_POOL ////////////////////////////

/** template for a user's pool; also see \ref t2t_message_base::pool_t.
 * \param T  the user's derived message class. */
template <class BaseT, class... derivedTs>
class t2t_pool : public __t2t_pool
{
public:
    static const int buffer_size = largest_type<BaseT,derivedTs...>::size;
    /** \brief constructor for a pool, also see \ref t2t_message_base::pool_t.
     * \param _num_bufs_init  how many buffers to put in pool initially.
     * \param _bufs_to_add_when_growing  if you use get(wait==-2) and it
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
        : __t2t_pool(buffer_size, _num_bufs_init,
                     _bufs_to_add_when_growing,
                     pmattr, pcattr) { }
    virtual ~t2t_pool(void) { }

    /** get a new message from the pool and specify how long to wait.
     * the refcount on the returned message will be 1.
     * \return  true if allocation succeeded, false, if pool empty.
     * \param ptr   pointer to new object returned here.
     * \param wait_ms  how long to wait, \ref wait_flag :
     *           <ul> <li> -2 : grow pool if empty </li>
     *                <li> -1 : wait forever </li>
     *                <li> 0 : don't wait at all, if the pool is empty,
     *                     return NULL immediately. </li>
     *                <li> >0 : wait for some milliseconds for a buffer
     *                     to become available, then give up </li> </ul>
     * \param args  if class T has a constructor, you must pass the args
     *              to the constructor here. */
    template <class T, typename... ConstructorArgs>
    bool alloc(t2t_shared_ptr<T> * ptr, int wait_ms,
               ConstructorArgs&&... args);

    __T2T_EVIL_CONSTRUCTORS(t2t_pool);
    __T2T_EVIL_DEFAULT_CONSTRUCTOR(t2t_pool);
};

//////////////////////////// T2T_QUEUE ////////////////////////////

/** template for a FIFO queue of messages.
 * \param BaseT  the user's base message class */
template <class BaseT>
class t2t_queue
{
    template <class queuesetBaseT> friend class t2t_queue_set;
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
              pthread_condattr_t *pcattr = NULL);
    virtual ~t2t_queue(void) { }

    /** enqueue a message into this queue; the message must come
     *  from a pool of the same type. note the queue is a FIFO.
     * \param msg  message to enqueue
     * \note   this does a take() on the shared_ptr, so the user's
     *     t2t_shared_ptr is now empty.
     * \note   it is safe for multiple threads to enqueue messages
     *     to a single queue. that is an expected use case. */
    template <class T> void enqueue(t2t_shared_ptr<T> &msg);

    /** dequeue a message from this queue in FIFO order.
     * \param wait_ms  how long to wait: \ref wait_flag
     *           <ul> <li> -1 : wait forever </li>
     *                <li> 0 : don't wait at all, if the pool is empty,
     *                     return NULL immediately. </li>
     *                <li> >0 : wait for some milliseconds for a buffer
     *                     to become available, then give up </li> </ul>
     * \note the dequeue method is NOT safe to call from multiple
     *       threads on the same queue at the same time. (multiple
     *       threads dequeueing from different queues is fine.)
     *       someone is going to want to try "load balancing" by
     *       having multiple threads dequeue from the same queue to
     *       spread out requests to multiple cores; this is not
     *       supported by this API, is not safe, and will probably
     *       throw assertions.
     * \note it is NOT safe to call this dequeue method if this
     *       queue has been added to a t2t_queue_set. */
    t2t_shared_ptr<BaseT>  dequeue(int wait_ms);

    __T2T_EVIL_CONSTRUCTORS(t2t_queue<BaseT>);
    __T2T_EVIL_NEW(t2t_queue<BaseT>);
    __T2T_EVIL_DEFAULT_CONSTRUCTOR(t2t_queue<BaseT>);
};

//////////////////////////// T2T_QUEUE_SET ////////////////////////////

/** template for a set of queues.
 * \param BaseT the user's base message class that all member queues
 *              are based on.
 */
template <class BaseT>
class t2t_queue_set
{
    __t2t_queue_set  qs;
public:
    /** constructor, requires attributes for the mutex and condition
     * used when dequeuing from the set.
     * \note when a t2t_queue has been added to this set, that queue
     *       uses the mutex and condition from this set, rather than
     *       their own (in other words, all queues which are added
     *       to this set will use the \em same mutex and condition,
     *       in order to close certain race conditions).
     * \note this class is not multi-thread safe, that is you should
     *       not allow one thread to do add/remove while another does
     *       dequeue. that would be very bad. */
    t2t_queue_set(pthread_mutexattr_t *pmattr = NULL,
                  pthread_condattr_t  *pcattr = NULL);

    /** destructor will clean up the list of queues. */
    ~t2t_queue_set(void);

    /** add a queue to this set. an identifier will be associated
     * with the queue; this identifier is returned in dequeue().
     * may be done at any time.
     * \param q  the t2t_queue to add to this set. the queue must
     *           be based on the same base message class as this
     *           set.
     * \param id the identifier for this queue. this identifier
     *    will be returned to the caller of dequeue(). this value
     *    will also serve as the priority of this queue relative to the
     *    other queues in this set. if all queues are empty at the
     *    entry to dequeue, this value will have no effect; dequeue()
     *    will wake up and return the first message enqueued to any of
     *    the queues in this set. \em however, if multiple queues have
     *    waiting messages, this value will determine which queues is
     *    dequeued first.
     * \note this class is not multi-thread safe, that is you should
     *       not allow one thread to do add/remove while another does
     *       dequeue. that would be very bad. */
    void add_queue(t2t_queue<BaseT> *q, int id);

    /** remove a queue from this set. may be done at any time.
     * \note this class is not multi-thread safe, that is you should
     *       not allow one thread to do add/remove while another does
     *       dequeue. that would be very bad. */
    void remove_queue(t2t_queue<BaseT> *q);

    /** monitor all queues added to this set, and dequeue a message
     * as soon as one becomes available in any queues in this set.
     * \param wait_ms  how long to wait: \ref wait_flag
     *           <ul> <li> -1 : wait forever </li>
     *                <li> 0 : don't wait at all, if the pool is empty,
     *                     return NULL immediately. </li>
     *                <li> >0 : wait for some milliseconds for a buffer
     *                     to become available, then give up </li> </ul>
     * \param id  a pointer to a user supplied variable to receive the
     *      identifier passed to add_queue(). this will inform the user
     *      which queue became active. if the user does not require this
     *      information, this argument may be omitted (default to NULL).
     * \note this class is not multi-thread safe, that is you should
     *       not allow one thread to do add/remove while another does
     *       dequeue. that would be very bad.
     * \note the dequeue method is NOT safe to call from multiple
     *       threads at the same time.
     * \note it is NOT safe to call an individual queue's dequeue
     *       method if that queue has been added to a set. */
    t2t_shared_ptr<BaseT> dequeue(int wait_ms, int *id = NULL);
};

//////////////////////////// T2T_MESSAGE_BASE ////////////////////////////

/** base class for all T2T messages. 
 * \param BaseT      the derived data type's base class name. */
template <class BaseT>
class t2t_message_base
{
protected:
    t2t_message_base(void) : __refcount(0) { }
    virtual ~t2t_message_base(void) { }
    // users don't delete; users should be using t2t_shared_ptr.
    static void operator delete(void *ptr);

private:
    __t2t_pool * __pool;

    // this is a 'no-throw' version of operator new,
    // which returns NULL instead of throwing.
    // this is important, because it prevents calling
    // the constructor function on a NULL.
    // t2t_pool.alloc is what invokes new().
    static void * operator new(size_t wanted_sz,
                               __t2t_pool *pool,
                               int wait_ms) throw ();
    template <class poolBaseT,
              class... poolDerivedTs> friend class t2t_pool;

    // t2t_shared_ptr needs to access __refcount
    std::atomic_int  __refcount;
    template <class sharedPtrBaseT> friend class t2t_shared_ptr;

    __T2T_EVIL_CONSTRUCTORS(t2t_message_base);
    __T2T_EVIL_NEW(t2t_message_base);
};

#define __T2T_INCLUDE_INTERNAL__ 2
#include "threadslinger2_internal.h"
#undef  __T2T_INCLUDE_INTERNAL__


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
 <li> \ref ThreadSlinger2::t2t_shared_ptr
 <li> \ref ThreadSlinger2::wait_flag
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

Suppose you had three messages you wanted to pass in a system, a base
class messages and two derived classes. Define them as below, deriving the
first class from ThreadSlinger2::t2t_message_base. Please note it is
important to list all derived types in the t2t_message_base template
argument list, so the code knows how big the buffers in the pool need
to be (for whatever is the largest of the derived types).

\note I recommend declaring \ref ThreadSlinger2::t2t_shared_ptr
  typedefs called "sp_t" in your derived classes to simplify casting, as
  you will see in the sample receiver code below. The t2t_message_base
  class will define an "sp_t" shared_ptr typedef for you, but the derived
  classes do not.

\code
class my_message_derived1; // forward
class my_message_derived2; // forward
class my_message_base
    : public ThreadSlinger2::t2t_message_base<
                 my_message_base,
                 my_message_derived1,
                 my_message_derived2>
{
public:
    my_message_base(  constructor_args  );
    virtual ~my_message_base( void );
    // your data, and any other methods (including virtual!) go here
    virtual void print(void) const { <print stuff> }

};

class my_message_derived1 : public my_message_base
{
public:
    // convenience
    typedef ThreadSlinger2::t2t_shared_ptr<my_message_derived1> sp_t;
    my_message_derived1(  constructor_args  );
    ~my_message_derived1( void );
    // your data, and any other methods go here
    virtual void print(void) const override { <print stuff> }
};

class my_message_derived2 : public my_message_base
{
public:
    // convenience
    typedef ThreadSlinger2::t2t_shared_ptr<my_message_derived2> sp_t;
    my_message_derived2(  constructor_args  );
    ~my_message_derived2( void );
    // your data, and any other methods go here
    virtual void print(void) const override { <print stuff> }
};
\endcode

Some of the functions below return a
\ref ThreadSlinger2::t2t_shared_ptr
which automatically manages reference counts in the returned
message. The user may assign to another shared_ptr or keep
this shared pointer as long as it wants.  Just like std::shared_ptr,
when the last reference is lost, the message will be destructed,
except here, it will be returned to the pool.

Note that ThreadSlinger2::t2t_message_base automatically defines three
convienient data types to help define pools and queues.

\code
template <class BaseT, class... DerivedTs>
class t2t_message_base
{
public:
    typedef t2t_pool<BaseT,DerivedTs...> pool_t;
    typedef t2t_queue<BaseT> queue_t;
    typedef t2t_shared_ptr<BaseT> sp_t;

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
    // each time "get" is called with wait==-2  and the pool is
    // empty, add 10 more buffers.  When we have to lock a mutex
    // and when we have to sleep on a condition for more buffers,
    // use the supplied mutex and cond attributes, or pass NULL
    // for defaults.
    my_message_base::pool_t   mypool(2,10,&mattr,&cattr);

    // create a queue which can pass my_message_base and it's
    // derived types.
    my_message_base::queue_t  myqueue(&mattr,&cattr);

    pthread_mutexattr_destroy(&mattr);
    pthread_condattr_destroy(&cattr);
\endcode

Now, a sender may allocate new buffers from the pool, and any arguments
required for the object constructors are passed through the arguments
to the \ref ThreadSlinger2::t2t_pool::alloc call.

Once it has allocated a buffer and filled it out, it enqueues it using
\ref ThreadSlinger2::t2t_queue::enqueue.

\code
    my_message_base::sp_t  spmb;
    if (mypool.alloc(&spmb,
               <time to wait>,
               <args to my_message_base constructor>))
    {
        spmb->field = value;  // etc
        printf("enqueuing a message NOW\n");
        myqueue.enqueue(spmb); // q takes a ref
        spmb.reset(); // release our ref
    }
\endcode

A sender may also allocate a derived message type too, using the same
syntax; note if the type specified is not actually a base type, or if
you didn't specify the derived class type in the t2t_message_base
parameter list and the buffers in the pool are too small for this type,
you will get compile-time errors telling you this.

Also note, this example shows you how to specify that the pool should
grow if it is currently empty.

\code
    my_message_derived2::sp_t  spmd2;
    if (mypool.alloc(&spmd2,
               ThreadSlinger2::GROW,
               <args for my_message_d2 constructor>))
    {
        spmd2->field = value; // etc
        printf("enqueuing a message NOW\n");
        myqueue.enqueue(spmd2); // takes a ref
        spmd2.reset(); // release our ref
    }
\endcode

The recipient thread then dequeues from this queue using
\ref ThreadSlinger2::t2t_queue::dequeue
to process the messages in its own time.

\ref ThreadSlinger2::t2t_shared_ptr has a helpful
\ref ThreadSlinger2::t2t_shared_ptr::cast
method which is useful for doing dynamic_cast from base class
types to derived types. This counts as an additional reference
against the message object.

\code
    while (keep_going)
    {
        my_message_base::sp_t x;
        // wait for up to 1000 mS (one second) for a message.
        x = myqueue->dequeue(1000);
        if (x) // t2t_shared_ptr supports checking for empty
        {
            printf("READER GOT MSG:\n");
            x->print();
            my_message_derived1::sp_t  y;
            if (y.cast(x))
                printf("got a derived 1! values = <format flags>\n",
                       y->derived1_field);
            my_message_derived2::sp_t  z;
            if (z.cast(x))
                printf("got a derived 2! values = <format flags>\n",
                       z->derived2_field);
        } // y and z go out of scope here, releasing their references
        else
            printf("READER GOT NULL\n");
    } // x goes out of scope here, releasing its reference
\endcode

Finally, by default errors are caught and printed on stderr; fatal errors
cause an exit of the process. If you want your own handler for ThreadSlinger2
errors, you can register a function to handle them using the global variable
\ref ThreadSlinger2::ts2_assert_handler.

\note If your assert handler is passed fatal=true, your assert handler
   \em must not continue execution of this thread. To continue execution
   is to invite SEGV or other errors.

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
   ThreadSlinger2::ts2_assert_handler = &custom_ts2_assert_handler;
}

\endcode


 */

//}; // namespace ThreadSlinger2
