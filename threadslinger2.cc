
#include "threadslinger2.h"

namespace ThreadSlinger2 {

static void default_ts2_assert_handler(ts2_error_t e,
                                       bool fatal,
                                       const char *filename,
                                       int lineno)
{
    fprintf(stderr, "\n\nERROR: ThreadSlinger2 ASSERTION %d at %s:%d\n\n",
            filename, lineno);
    if (fatal)
        // if you dont like this exiting, CHANGE IT
        exit(1);
}

ts2_assert_handler_t ts2_assert_handler = &default_ts2_assert_handler;

//////////////////////////// T2T_CONTAINER ////////////////////////////

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
    void *operator new(size_t ignore_sz, int real_size)
    {
        return malloc(real_size + sizeof(__t2t_container));
    }
    void  operator delete(void *ptr)
    {
        free(ptr);
    }

    __T2T_EVIL_CONSTRUCTORS(__t2t_container);
    __T2T_EVIL_NEW(__t2t_container);
};

//////////////////////////// T2T_POOL ////////////////////////////

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

// if grow=true, ignore wait_ms; if grow=false,
// -1 : wait forever, 0 : dont wait, >0 wait for some mS
void * __t2t_pool :: alloc(int wait_ms, bool grow /*= false*/)
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

void __t2t_pool :: release(void * ptr)
{
    __t2t_buffer_hdr * h = (__t2t_buffer_hdr *) ptr;
    h--;
    if (h->list != NULL)
    {
        TS2_ASSERT(T2T_POOL_RELEASE_ALREADY_ON_LIST,true);
    }
    if (h->inuse == false)
    {
        TS2_ASSERT(DOUBLE_FREE,false);
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

//////////////////////////// T2T_QUEUE ////////////////////////////

__t2t_queue :: __t2t_queue(pthread_mutexattr_t *pmattr,
                           pthread_condattr_t *pcattr)
{
    pthread_mutex_init(&mutex, pmattr);
    pthread_cond_init(&cond, pcattr);
    if (pcattr)
        pthread_condattr_getclock(pcattr, &clk_id);
    else
        // the default condattr clock appears to be REALTIME
        clk_id = CLOCK_REALTIME;
    buffers.init();
    waiting_cond = NULL;
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
    lock();
    if (waiting_cond != NULL)
    {
        TS2_ASSERT(T2T_QUEUE_MULTIPLE_THREAD_DEQUEUE,false);
        unlock();
        return NULL;
    }
    if (wait_ms < 0)
    {
        // wait forever
        while (buffers.empty())
        {
            waiting_cond = &cond;
            pthread_cond_wait(&cond, &mutex);
        }
    }
    else if (wait_ms == 0)
    {
        if (buffers.empty())
        {
            unlock();
            return NULL;
        }
    }
    else // wait_ms > 0
    {
        bool first = true;
        __t2t_timespec  ts;
        while (buffers.empty())
        {
            waiting_cond = &cond;
            if (first)
            {
                __t2t_timespec t(wait_ms);
                ts.getNow(clk_id);
                ts += t;
                first = false;
            }
            int ret = pthread_cond_timedwait(&cond, &mutex, &ts);
            waiting_cond = NULL;
            if (ret != 0)
            {
                unlock();
                return NULL;
            }
        }
    }
    h = buffers.get_head();
    h->ok();
    if (!_validate(h))
        TS2_ASSERT(T2T_QUEUE_DEQUEUE_NOT_ON_THIS_LIST,true);
    h->remove();
    unlock();
    return h;
}

//static
__t2t_buffer_hdr * __t2t_queue :: _dequeue_multi(int num_qs,
                                                 __t2t_queue **qs,
                                                 int *which_q,
                                                 int wait_ms)
{
    __t2t_buffer_hdr * h = NULL;
    __t2t_queue * q0 = qs[0];
    int qind = -1;
    bool first = true;
    __t2t_timespec  ts;

    for (int ind = 0; ind < num_qs; ind++)
    {
        __t2t_queue * q = qs[ind];
        q->lock();
        if (q->waiting_cond != NULL)
        {
            TS2_ASSERT(T2T_QUEUE_MULTIPLE_THREAD_DEQUEUE,false);
            // dont bail, just overwrite cuz its busted anyway.
        }
        q->waiting_cond = &q0->cond;
        q->unlock();
    }

    do {
        for (int ind = 0; h == NULL && ind < num_qs; ind++)
        {
            __t2t_queue * q = qs[ind];
            q->lock();
            if (q->buffers.empty() == false)
            {
                h = q->buffers.get_head();
                if (!q->_validate(h))
                    TS2_ASSERT(T2T_QUEUE_DEQUEUE_NOT_ON_THIS_LIST,true);
                h->remove();
                qind = ind;
            }
            q->unlock();
        }
        if (h)
            break;
        if (wait_ms == 0)
            break;
        else if (wait_ms < 0)
        {
            q0->lock();
            pthread_cond_wait(&q0->cond, &q0->mutex);
            q0->unlock();
        }
        else // wait_ms > 0
        {
            if (first)
            {
                __t2t_timespec t(wait_ms);
                ts.getNow(q0->clk_id);
                ts += t;
                first = false;
            }
            q0->lock();
            int ret = pthread_cond_timedwait(&q0->cond,
                                             &q0->mutex,
                                             &ts);
            q0->unlock();
            if (ret != 0)
                break;
        }
    } while (h == NULL);

    for (int ind = 0; ind < num_qs; ind++)
    {
        __t2t_queue * q = qs[ind];
        q->lock();
        q->waiting_cond = NULL;
        q->unlock();
    }
    if (which_q)
        *which_q = qind;
    return h;
}

void __t2t_queue :: _enqueue(__t2t_buffer_hdr *h)
{
    h->ok();
    if (h->list != NULL)
    {
        TS2_ASSERT(T2T_QUEUE_ENQUEUE_ALREADY_ON_A_LIST,false);
        return;
    }
    lock();
    buffers.add_head(h);
    pthread_cond_t *pcond = waiting_cond;
    waiting_cond = NULL;
    unlock();
    if (pcond)
        pthread_cond_signal(pcond);
}

void __t2t_queue :: _enqueue_tail(__t2t_buffer_hdr *h)
{
    h->ok();
    if (h->list != NULL)
    {
        TS2_ASSERT(T2T_QUEUE_ENQUEUE_ALREADY_ON_A_LIST,false);
        return;
    }
    lock();
    buffers.add_tail(h);
    pthread_cond_t *pcond = waiting_cond;
    waiting_cond = NULL;
    unlock();
    if (pcond)
        pthread_cond_signal(pcond);
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
