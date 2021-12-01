#if 0
set -e -x

incs="-I../libpfkutil"
srcs="threadslinger2.cc threadslinger2_test.cc"
libs="../libpfkutil/signal_backtrace.cc ../libpfkutil/dll3.cc -lpthread"
defs=""
cflags="-O0 -g3"
lflags=""

g++ $cflags $lflags $defs $incs $srcs $libs  -o t

if [[ "x$1" = "x" ]] ; then
    ./t
fi
exit 0
;
#endif

#include "threadslinger2.h"

using namespace std;

class my_message : public ThreadSlinger2::t2t_message_base<my_message>
{
public:
    int a;
    int b;
    my_message(int _a, int _b)
        : a(_a), b(_b)
    {
        printf("constructing my_message %d,%d\n", a,b);
    }
    virtual ~my_message(void)
    {
        printf("destructing my_message %d,%d\n", a,b);
    }
};

void printstats(my_message::pool_t *pool, const char *what)
{
    ThreadSlinger2::t2t_pool_stats  stats;
    pool->get_stats(stats);
    cout << what << ": " << stats << endl;
}

bool die_already = false;

void *reader_thread(void *arg)
{
    my_message::queue_t * q = (my_message::queue_t *) arg;
    my_message::queue_t * qs[1] = { q };

    while (!die_already)
    {
        my_message * m = NULL;
        int which_q = -1;
        printf("READER entering dequeue(2000)\n");
//      m = q->dequeue(2000);
        m = my_message::queue_t::dequeue_multi(1, qs,
                                               &which_q, 2000);
        if (m)
        {
            printf("READER GOT MSG\n");
            m->deref();
        }
        else
            printf("READER GOT NULL\n");
    }

    return NULL;
}



int main(int argc, char ** argv)
{
    pthread_mutexattr_t  mattr;
    pthread_condattr_t   cattr;

    pthread_mutexattr_init(&mattr);
    pthread_condattr_init(&cattr);
//    pthread_condattr_setclock(&cattr, CLOCK_REALTIME);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);

    // create a pool of my_message, prepopulated
    // with 1 message, and if grow=true, grows ten at a time.
    my_message::pool_t * mypool =
        new my_message::pool_t(2,10,&mattr,&cattr);

    my_message::queue_t    q(&mattr,&cattr);

    pthread_mutexattr_destroy(&mattr);
    pthread_condattr_destroy(&cattr);

    my_message * m;

    printstats(mypool, "before first alloc");

    // this should succeed.
    printf("attempting first alloc, should succeed\n");
    m = new(mypool) my_message(1,2);
    if (m)
    {
        printf("enqueuing a message NOW\n");
        q.enqueue(m);
    }
    else
        printf("FAILED\n");

    printstats(mypool, "after first alloc");

    printf("attempting second alloc\n");
    m = new(mypool,1000) my_message(3,4);
    if (m)
    {
        printf("enqueuing a message NOW\n");
        q.enqueue(m);
    }
    else
        printf("FAILED\n");

    printstats(mypool, "after second alloc");



    pthread_t id;
    pthread_attr_t       attr;
    pthread_attr_init   (&attr);
    pthread_create(&id, &attr, &reader_thread, &q);
    pthread_attr_destroy(&attr);




    // this should succeed.
    printf("attempting third alloc\n");
    m = new(mypool,T2T_GROW) my_message(5,6);
    if (m)
    {
        printf("enqueuing a message NOW\n");
        q.enqueue(m);
    }
    else
        printf("FAILED\n");

    printstats(mypool, "after third alloc");

    printf("sleeping 3\n");
    sleep(3);
    printf("done sleeping 3\n");

    printf("telling READER to die and waiting\n");
    die_already =true;
    pthread_join(id, NULL);
    printf("READER is DEAD\n");

    printf("deleting pool\n");
    delete mypool;
    printf("delete pool done\n");

    return 0;
}
