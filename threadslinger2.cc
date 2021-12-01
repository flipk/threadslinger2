
#include "threadslinger2.h"

namespace ThreadSlinger2 {

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
            if (ret != 0)
            {
                unlock();
                return NULL;
            }
        }
    }
    h = buffers.__t2t_get_next();
    h->ok();
    if (!_validate(h))
        printf("_dequeue VALIDATION FAIL\n");
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
        q->waiting_cond = &q0->cond;
        q->unlock();
    }

    do {
        for (int ind = 0; ind < num_qs; ind++)
        {
            __t2t_queue * q = qs[ind];
            q->lock();
            if (q->buffers.empty() == false)
            {
                h = q->buffers.__t2t_get_next();
                if (!q->_validate(h))
                    printf("_dequeue_multi VALIDATION FAIL\n");
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
    if (h->__t2t_list != NULL)
    {
        printf("_enqueue ALREADY ON A LIST\n");
        return;
    }
    lock();
    buffers.add(h);
    pthread_cond_t *pcond = waiting_cond;
    waiting_cond = NULL;
    unlock();
    if (pcond)
        pthread_cond_signal(pcond);
}

void __t2t_queue :: _enqueue_tail(__t2t_buffer_hdr *h)
{
    h->ok();
    if (h->__t2t_list != NULL)
    {
        printf("_enqueue ALREADY ON A LIST\n");
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
