
#include "threadslinger2.h"

namespace ThreadSlinger2 {

//////////////////////////// ERROR HANDLING ////////////////////////////

const char * ts2_error_types[T2T_NUM_ERRORS] = {

    // please keep this in sync with ts2_error_t

    "zero entry not used",
    "BUFFER_SIZE_TOO_BIG_FOR_POOL",
    "T2T_LINKS_MAGIC_CORRUPT",
    "T2T_LINKS_ADD_ALREADY_ON_LIST",
    "T2T_LINKS_REMOVE_NOT_ON_LIST",
    "T2T_POOL_RELEASE_ALREADY_ON_LIST",
    "DOUBLE_FREE",
    "T2T_QUEUE_MULTIPLE_THREAD_DEQUEUE",
    "T2T_QUEUE_DEQUEUE_NOT_ON_THIS_LIST",
    "T2T_QUEUE_ENQUEUE_ALREADY_ON_A_LIST",
    "T2T_QUEUE_SET_EMPTY",
    "T2T_ENQUEUE_EMPTY_POINTER"
};

static void default_ts2_assert_handler(ts2_error_t e,
                                       bool fatal,
                                       const char *filename,
                                       int lineno)
{
    fprintf(stderr, "\n\nERROR: ThreadSlinger2 ASSERTION %d at %s:%d\n\n",
            e, filename, lineno);
    if (fatal)
        // if you dont like this exiting, CHANGE IT
        exit(1);
}

ts2_assert_handler_t ts2_assert_handler = &default_ts2_assert_handler;

//////////////////////////// T2T_POOL_STATS ////////////////////////////

t2t_pool_stats :: t2t_pool_stats(int _buffer_size /*= 0*/)
{
    init(_buffer_size);
}

void t2t_pool_stats :: init(int _buffer_size)
{
    buffer_size = _buffer_size;
    total_buffers = 0;
    buffers_in_use = 0;
    alloc_fails = 0;
    grows = 0;
    double_frees = 0;
}

//////////////////////////// __T2T_CONTAINER ////////////////////////////

struct __t2t_container
{
    // this is required by unique_ptr, because it has
    // a static_assert(sizeof(_Tp)>0) in it.....
    int dummy;
    uint64_t data[0]; // forces entire struct to 8 byte alignment
    __t2t_container(void)
    {
        dummy = 0;
    }
    void *operator new(size_t struct_sz, int real_size)
    {
        return malloc(struct_sz + real_size);
    }
    void  operator delete(void *ptr)
    {
        free(ptr);
    }

    __T2T_EVIL_CONSTRUCTORS(__t2t_container);
    __T2T_EVIL_NEW(__t2t_container);
};

//////////////////////////// __T2T_POOL ////////////////////////////

__t2t_pool :: __t2t_pool(int buffer_size,
                         int _num_bufs_init,
                         int _bufs_to_add_when_growing,
                         pthread_mutexattr_t *pmattr,
                         pthread_condattr_t *pcattr)
    : stats(buffer_size), q(pmattr, pcattr)
{
    bufs_to_add_when_growing = _bufs_to_add_when_growing;
    add_bufs(_num_bufs_init);
}

//virtual
__t2t_pool :: ~__t2t_pool(void)
{
}

void __t2t_pool :: add_bufs(int num_bufs)
{
    if (num_bufs <= 0)
        return;
    int real_buffer_size = stats.buffer_size + sizeof(__t2t_buffer_hdr);
    int container_size = num_bufs * real_buffer_size;
    __t2t_container * c = new(container_size) __t2t_container;
    container_pool.push_back(std::unique_ptr<__t2t_container>(c));
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

// wait_ms (see enum wait_flag):
// -2 : grow if empty
// -1 : wait forever,
//  0 : don't wait
// >0 : wait for some mS
void * __t2t_pool :: _alloc(int wait_ms)
{
    __t2t_buffer_hdr * h = NULL;
    if (wait_ms == GROW)
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

void __t2t_pool :: release(void * ptr)
{
    __t2t_buffer_hdr * h = (__t2t_buffer_hdr *) ptr;
    h--;
    if (h->list != NULL)
    {
        __TS2_ASSERT(T2T_POOL_RELEASE_ALREADY_ON_LIST,true);
    }
    if (h->inuse == false)
    {
        __TS2_ASSERT(DOUBLE_FREE,false);
        stats.double_frees ++;
    }
    else
    {
        h->inuse = false;
        q._enqueue(h);
        stats.buffers_in_use --;
    }
}

void __t2t_pool :: get_stats(t2t_pool_stats &_stats) const
{
    _stats = stats;
}

//////////////////////////// __T2T_QUEUE ////////////////////////////

__t2t_queue :: __t2t_queue(pthread_mutexattr_t *pmattr,
                           pthread_condattr_t *pcattr)
{
    __t2t_links::init();
    pthread_mutex_init(&mutex, pmattr);
    pthread_cond_init(&cond, pcattr);
    if (pcattr)
        pthread_condattr_getclock(pcattr, &clk_id);
    else
        // the default condattr clock appears to be REALTIME
        clk_id = CLOCK_REALTIME;
    buffers.init();
    psetmutex = NULL;
    psetcond = NULL;
    id = 0;
}

__t2t_queue :: ~__t2t_queue(void)
{
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
}

// -1 : wait forever
//  0 : dont wait, just return
// >0 : wait for some number of mS
__t2t_buffer_hdr * __t2t_queue :: _dequeue(int wait_ms)
{
    __t2t_buffer_hdr * h = NULL;
    Lock  l(&mutex);
    if (psetmutex != NULL)
    {
        __TS2_ASSERT(T2T_QUEUE_MULTIPLE_THREAD_DEQUEUE,false);
        return NULL;
    }
    if (wait_ms < 0)
    {
        // wait forever
        while (buffers.empty())
            pthread_cond_wait(&cond, &mutex);
    }
    else if (wait_ms == 0)
    {
        if (buffers.empty())
        {
            return NULL;
        }
    }
    else // wait_ms > 0
    {
        bool first = true;
        bool timed_out = false;
        __t2t_timespec  ts;
        while (buffers.empty() && !timed_out)
        {
            if (first)
            {
                __t2t_timespec t(wait_ms);
                ts.getNow(clk_id);
                ts += t;
                first = false;
            }
            int ret = pthread_cond_timedwait(&cond, &mutex, &ts);
            if (ret != 0)
                timed_out = true;
        }
        if (timed_out)
        {
            return NULL;
        }
    }
    h = buffers.get_next();
    h->ok();
    if (!_validate(h))
        __TS2_ASSERT(T2T_QUEUE_DEQUEUE_NOT_ON_THIS_LIST,true);
    h->remove();
    return h;
}

void __t2t_queue :: _enqueue(__t2t_buffer_hdr *h)
{
    h->ok();
    if (h->list != NULL)
    {
        __TS2_ASSERT(T2T_QUEUE_ENQUEUE_ALREADY_ON_A_LIST,false);
        return;
    }
    {
        Lock l(&mutex);
        buffers.add_next(h);
        if (psetmutex)
        {
            Lock l2(psetmutex);
            pthread_cond_signal(psetcond);
        }
    }
    pthread_cond_signal(&cond);
}

void __t2t_queue :: _enqueue_tail(__t2t_buffer_hdr *h)
{
    h->ok();
    if (h->list != NULL)
    {
        __TS2_ASSERT(T2T_QUEUE_ENQUEUE_ALREADY_ON_A_LIST,false);
        return;
    }
    {
        Lock l(&mutex);
        buffers.add_prev(h);
        if (psetmutex)
        {
            Lock l2(psetmutex);
            pthread_cond_signal(psetcond);
        }
    }
    pthread_cond_signal(&cond);
}

//////////////////////////// __T2T_QUEUE_SET ////////////////////////////

__t2t_queue_set ::__t2t_queue_set(pthread_mutexattr_t *pmattr /*= NULL*/,
                                  pthread_condattr_t  *pcattr /*= NULL*/)
{
    qs.init();
    pthread_mutex_init(&set_mutex, pmattr);
    pthread_cond_init(&set_cond, pcattr);

    if (pcattr)
        pthread_condattr_getclock(pcattr, &clk_id);
    else
        // the default condattr clock appears to be REALTIME
        clk_id = CLOCK_REALTIME;
}

__t2t_queue_set :: ~__t2t_queue_set(void)
{
    __t2t_queue * q;
    while ((q = qs.get_next()) != &qs)
        _remove_queue(q);
    pthread_mutex_destroy(&set_mutex);
    pthread_cond_destroy(&set_cond);
}

bool
__t2t_queue_set :: _add_queue(__t2t_queue *q, int id)
{
    __t2t_queue::Lock l(&set_mutex);

    if (q->list != NULL)
    {
        __TS2_ASSERT(QUEUE_IN_A_SET, false);
        return false;
    }

    if (qs.empty())
        qs.add_next(q);
    else
    {
        __t2t_queue * tq;
        for (tq = qs.get_next(); tq != &qs; tq = tq->get_next())
            if (tq->id > id)
                break;
        tq->add_prev(q);
    }
    q->set_pmutexpcond(&set_mutex, &set_cond);
    q->id = id;
    return true;
}

void
__t2t_queue_set :: _remove_queue(__t2t_queue *q)
{
    __t2t_queue::Lock l(&set_mutex);
    q->remove();
    q->set_pmutexpcond();
}

__t2t_buffer_hdr *
__t2t_queue_set :: _dequeue(int wait_ms, int *id)
{
    __t2t_queue * q;
    __t2t_buffer_hdr * h = NULL;
    int qid = -1;
    bool first = true;
    __t2t_timespec  ts;

    if (qs.empty())
    {
        __TS2_ASSERT(T2T_QUEUE_SET_EMPTY,false);
        if (id)
            *id = qid;
        return NULL;
    }

    __t2t_queue::Lock  l(&set_mutex);

    do {
        __t2t_queue * q0 = qs.get_next();
        for (q = q0; q != &qs; q = q->get_next())
        {
            __t2t_queue::Lock l(&q->mutex);
            if (q->buffers.empty() == false)
            {
                h = q->buffers.get_next();
                if (!q->_validate(h))
                    __TS2_ASSERT(T2T_QUEUE_DEQUEUE_NOT_ON_THIS_LIST,true);
                h->remove();
                qid = q->id;
                goto out;
            }
        }
        if (wait_ms == 0)
            goto out;
        else if (wait_ms < 0)
        {
            pthread_cond_wait(&set_cond, &set_mutex);
        }
        else // wait_ms > 0
        {
            if (first)
            {
                __t2t_timespec t(wait_ms);
                ts.getNow(clk_id);
                ts += t;
                first = false;
            }
            int ret = pthread_cond_timedwait(
                &set_cond, &set_mutex, &ts);
            if (ret != 0)
                goto out;
        }
    } while (h == NULL);

out:
    if (id)
        *id = qid;
    return h;
}

}; // namespace ThreadSlinger2

///////////////////////// STREAM OPS /////////////////////////

// this has to be outside the namespace
std::ostream &
operator<<(std::ostream &strm,
           const ThreadSlinger2::t2t_pool_stats &stats)
{
    strm << "bufsz " << stats.buffer_size
         << " total " << stats.total_buffers
         << " inuse " << stats.buffers_in_use
         << " allocfails " << stats.alloc_fails
         << " grows " << stats.grows
         << " doublefrees " << stats.double_frees;
    return strm;
}
