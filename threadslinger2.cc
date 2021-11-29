
#include "threadslinger2.h"

namespace ThreadSlinger2 {

//////////////////////////// T2T_CONTAINER ////////////////////////////


__t2t_container :: __t2t_container(int _container_size)
{
    container_size = _container_size;
}

void *__t2t_container :: operator new(size_t ignore_sz, int real_size)
{
    printf("new t2t_container of size %d\n", real_size);
    return malloc(real_size + sizeof(__t2t_container));
}

void __t2t_container :: operator delete(void *ptr)
{
    __t2t_container * c = (__t2t_container *) ptr;
    printf("deleting t2t_container of size %d\n",
           c->container_size);
    free(ptr);
}

//////////////////////////// T2T_QUEUE ////////////////////////////

__t2t_queue :: __t2t_queue(pthread_mutexattr_t *pmattr,
                           pthread_condattr_t *pcattr,
                           clockid_t  _clk)
{
    pthread_mutex_init(&mutex, pmattr);
    pthread_cond_init(&cond, pcattr);
    clk_id = _clk;
    buffers.init();
    waiting = false;
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
            waiting = true;
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
    if (!_validate(h))
        printf("_dequeue VALIDATION FAIL\n");
    h->remove();
    unlock();
    return h;
}

void __t2t_queue :: _enqueue(__t2t_buffer_hdr *h)
{
    if (h->__t2t_list != NULL)
    {
        printf("_enqueue ALREADY ON A LIST\n");
        return;
    }
    lock();
    buffers.add(h);
    bool signal = waiting;
    waiting = false;
    unlock();
    if (signal)
        pthread_cond_signal(&cond);
}

//////////////////////////// T2T_POOL ////////////////////////////

__t2t_pool :: __t2t_pool(int _buffer_size,
                         int _num_bufs_init,
                         int _bufs_to_add_when_growing,
                         pthread_mutexattr_t *pmattr,
                         pthread_condattr_t *pcattr,
                         clockid_t  _clk)
    : q(pmattr,pcattr,_clk)
{
    stats.init();
    stats.buffer_size = _buffer_size;
    bufs_to_add_when_growing = _bufs_to_add_when_growing;
    _add_bufs(_num_bufs_init);
}

__t2t_pool :: ~__t2t_pool(void)
{
    // the container_pool list deletes the
    // sp's which frees the containers
}

void __t2t_pool :: _add_bufs(int num_bufs)
{
    if (num_bufs <= 0)
        return;

    int real_buffer_size = stats.buffer_size + sizeof(__t2t_buffer_hdr);
    int container_size = num_bufs * real_buffer_size;
    __t2t_container * c;
    c = new(container_size) __t2t_container(container_size);
    container_pool.push_back(__t2t_container::up(c));

    uint8_t * ptr = (uint8_t *) c->data;

    for (int ind = 0; ind < num_bufs; ind++)
    {
        __t2t_buffer_hdr * h = (__t2t_buffer_hdr *) ptr;
        h->init(stats.buffer_size, container_size);
        stats.total_buffers ++;
        q._enqueue(h);
        ptr += real_buffer_size;
    }
}

// if grow=true, ignore wait_ms; if grow=false,
// -1 : wait forever, 0 : dont wait, >0 wait for some mS
__t2t_buffer_hdr * __t2t_pool :: _alloc(int wait_ms, bool grow /*= false*/)
{
    __t2t_buffer_hdr * h = NULL;
    if (grow)
    {
        h = q._dequeue(0);
        if (h == NULL)
        {
            _add_bufs(bufs_to_add_when_growing);
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
    return h;
}

void __t2t_pool :: _release(__t2t_buffer_hdr *h)
{
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
