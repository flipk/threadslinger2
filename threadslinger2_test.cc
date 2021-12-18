#if 0
set -e -x

incs=""
srcs="threadslinger2.cc threadslinger2_test.cc"
libs="-lpthread"
defs=""
cflags="-O3"
lflags=""
std="-std=c++11"

clear
g++ $std $cflags $lflags $defs $incs $srcs $libs  -o t

if [[ "x$1" = "x" ]] ; then
    script 0log -c ./t
fi
exit 0
;
#endif

#include "threadslinger2.h"

using namespace std;

namespace ts2 = ThreadSlinger2;
// neat, but not using them just now:
//template <class... T> using ts2mb = ts2::t2t_message_base<T...>;
//template <class    T> using ts2sp = ts2::t2t_shared_ptr  <T>;

class my_message_base : public ts2::t2t_message_base<my_message_base>
{
public:
    // convenience
    typedef ts2::t2t_queue<my_message_base> queue_t;
    typedef ts2::t2t_shared_ptr<my_message_base> sp_t;

    enum msgtype { TYPE_B, TYPE_D1, TYPE_D2 } t;
    int a;
    int b;
    my_message_base(int _a, int _b)
        : t(TYPE_B), a(_a), b(_b)
    {
        printf("constructing my_message_base type %d a=%d b=%d\n", t,a,b);
    }
    my_message_base(msgtype _t, int _a, int _b)
        : t(_t), a(_a), b(_b)
    {
        printf("constructing my_message_base type %d a=%d b=%d\n", t,a,b);
    }
    virtual ~my_message_base(void)
    {
        printf("destructing my_message_base type %d a=%d b=%d\n", t,a,b);
    }
    virtual void print(void) const
    {
        printf("virtual print: "
               "message type is my_message_base, a=%d b=%d\n", a, b);
    }
};

class my_message_derived1 : public my_message_base
{
public:
    // convenience
    typedef ts2::t2t_pool<my_message_base,
                          my_message_derived1> pool1_t;
    typedef ts2::t2t_shared_ptr<my_message_derived1> sp_t;

    int c;
    int d;
    my_message_derived1(int _a, int _b, int _c, int _d)
        : my_message_base(TYPE_D1, _a, _b), c(_c), d(_d)
    {
        printf("constructing my_message_derived1 c=%d d=%d\n", c,d);
    }
    ~my_message_derived1(void)
    {
        printf("destructing my_message_derived1 c=%d d=%d\n", c,d);
    }
    virtual void print(void) const override
    {
        printf("virtual print: "
               "message type is my_message_derived1, a=%d b=%d c=%d d=%d\n",
               a, b, c, d);
    }
};

class my_message_derived2 : public my_message_base
{
public:
    // convenience
    typedef ts2::t2t_pool<my_message_base,
                          my_message_derived2> pool2_t;
    typedef ts2::t2t_shared_ptr<my_message_derived2> sp_t;

    int e;
    int f;
    int g;
    my_message_derived2(int _a, int _b, int _e, int _f, int _g)
        : my_message_base(TYPE_D2, _a, _b), e(_e), f(_f), g(_g)
    {
        printf("constructing my_message_derived2 e=%d f=%d g=%d\n", e,f,g);
    }
    ~my_message_derived2(void)
    {
        printf("destructing my_message_derived2 e=%d f=%d g=%d\n", e,f,g);
    }
    virtual void print(void) const override
    {
        printf("virtual print: "
               "message type is my_message_derived2, "
               "a=%d b=%d e=%d f=%d g=%d\n",
               a, b, e, f, g);
    }
};

template <class BaseT, class... derivedTs>
void printstats(ts2::t2t_pool<BaseT,derivedTs...> *pool,
                const char *what)
{
    ts2::t2t_pool_stats  stats;
    pool->get_stats(stats);
    cout << what << ": " << stats << endl;
}

bool die_already = false;

struct thread_info {
    static const int max_queues = 100; // arbitrary
    int num_queues;
    my_message_base::queue_t* qs[max_queues];
};

void *reader_thread(void *arg);

int main(int argc, char ** argv)
{
    pthread_mutexattr_t  mattr;
    pthread_condattr_t   cattr;

    pthread_mutexattr_init(&mattr);
    pthread_condattr_init(&cattr);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);

    my_message_derived1::pool1_t  mypool1(1,10,&mattr,&cattr);
    my_message_derived2::pool2_t  mypool2(1,10,&mattr,&cattr);
    my_message_base    ::queue_t  myqueue1(    &mattr,&cattr);
    my_message_base    ::queue_t  myqueue2(    &mattr,&cattr);

    pthread_mutexattr_destroy(&mattr);
    pthread_condattr_destroy(&cattr);

    printstats(&mypool1, "1");
    printstats(&mypool2, "2");

    printf("attempting first alloc\n");
    my_message_base::sp_t  spmb;
    if (mypool1.alloc(&spmb,
                      ts2::NO_WAIT,
                      1,2))
    {
        printf("enqueuing a message NOW\n");
        myqueue1.enqueue(spmb);
    }
    else
        printf("FAILED\n");

    printstats(&mypool1, "1");
    printstats(&mypool2, "2");

    printf("attempting second alloc\n");
    my_message_derived1::sp_t  spmd1;
    if (mypool1.alloc(&spmd1,
                     ts2::GROW,
                     3,4,5,6))
    {
        printf("enqueuing a message NOW\n");
        myqueue2.enqueue(spmd1);
    }
    else
        printf("FAILED\n");

    printstats(&mypool1, "1");
    printstats(&mypool2, "2");

    printf("attempting third alloc\n");
    my_message_derived2::sp_t  spmd2;
    if (mypool2.alloc(&spmd2,
                      1000,
                     7,8,9,10,11))
    {
        printf("enqueuing a message NOW\n");
        myqueue2.enqueue(spmd2);
    }
    else
        printf("FAILED\n");

    printstats(&mypool1, "1");
    printstats(&mypool2, "2");

    printf("\nnow starting reader thread:\n");

    // now that three enqueues have been done, start the reader.
    thread_info  ti;

    ti.num_queues = 0;
    ti.qs[ti.num_queues++] = &myqueue1;
    ti.qs[ti.num_queues++] = &myqueue2;

    pthread_attr_t       attr;
    pthread_attr_init   (&attr);
    pthread_t       id;
    pthread_create(&id, &attr, &reader_thread, &ti);
    pthread_attr_destroy(&attr);

    sleep(1);

    die_already = true;
    pthread_join(id, NULL);

    printstats(&mypool1, "1");
    printstats(&mypool2, "2");

    return 0;
}

void *reader_thread(void *arg)
{
    thread_info * ti = (thread_info *) arg;

    while (!die_already)
    {
        my_message_base::sp_t x;
        int which_q = -1;

        printf("READER entering dequeue()\n");

        x = my_message_base::queue_t::dequeue_multi(
            ti->num_queues, ti->qs, &which_q, 250);

        if (x)
        {
            printf("READER GOT MSG on queue %d:\n", which_q);
            x->print();

            my_message_derived1::sp_t  y;
            // this one tests the casting operator=
            if (y = x)
                printf("dynamic cast to md1 is OK! c,d = %d,%d\n",
                       y->c, y->d);

            // this one tests the casting constructor
            my_message_derived2::sp_t  z = x;
            if (z)
                printf("dynamic cast to md2 is OK! e,f,g = %d,%d,%d\n",
                       z->e, z->f, z->g);
        }
        else
            printf("READER GOT NULL\n");
    }

    return NULL;
}
