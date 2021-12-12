#if 0
set -e -x

incs=""
srcs="threadslinger2.cc threadslinger2_test.cc"
libs="-lpthread"
defs=""
cflags="-O3"
lflags=""

clear
g++ $cflags $lflags $defs $incs $srcs $libs  -o t

if [[ "x$1" = "x" ]] ; then
    script 0log -c ./t
fi
exit 0
;
#endif

#include "threadslinger2.h"

using namespace std;

class my_message_d1;
class my_message_d2;
class my_message_b
    : public ThreadSlinger2::t2t_message_base<my_message_b,
                                              my_message_d1,
                                              my_message_d2>
{
public:
    enum msgtype { TYPE_B, TYPE_D1, TYPE_D2 } t;
    int a;
    int b;
    my_message_b(int _a, int _b)
        : t(TYPE_B), a(_a), b(_b)
    {
        printf("constructing my_message_b type %d a=%d b=%d\n", t,a,b);
    }
    my_message_b(msgtype _t, int _a, int _b)
        : t(_t), a(_a), b(_b)
    {
        printf("constructing my_message_b type %d a=%d b=%d\n", t,a,b);
    }
    virtual ~my_message_b(void)
    {
        printf("destructing my_message_b type %d a=%d b=%d\n", t,a,b);
    }
    virtual void print(void)
    {
        printf("virtual print: "
               "message type is my_message_b, a=%d b=%d\n", a, b);
    }
};

class my_message_d1 : public my_message_b
{
public:
    int c;
    int d;
    my_message_d1(int _a, int _b, int _c, int _d)
        : my_message_b(TYPE_D1, _a, _b), c(_c), d(_d)
    {
        printf("constructing my_message_d1 c=%d,d=%d\n", c,d);
    }
    ~my_message_d1(void)
    {
        printf("destructing my_message_d1 c=%d,d=%d\n", c,d);
    }
    virtual void print(void)
    {
        printf("virtual print: "
               "message type is my_message_d1, a=%d b=%d c=%d d=%d\n",
               a, b, c, d);
    }
};

class my_message_d2 : public my_message_b
{
public:
    int e;
    int f;
    int g;
    my_message_d2(int _a, int _b, int _e, int _f, int _g)
        : my_message_b(TYPE_D2, _a, _b), e(_e), f(_f), g(_g)
    {
        printf("constructing my_message_d2 e=%d f=%d g=%d\n", e,f,g);
    }
    ~my_message_d2(void)
    {
        printf("destructing my_message_d2 e=%d f=%d g=%d\n", e,f,g);
    }
    virtual void print(void)
    {
        printf("virtual print: "
               "message type is my_message_d2, a=%d b=%d e=%d f=%d g=%d\n",
               a, b, e, f, g);
    }
};

void printstats(my_message_b::pool_t *pool, const char *what)
{
    ThreadSlinger2::t2t_pool_stats  stats;
    pool->get_stats(stats);
    cout << what << ": " << stats << endl;
}

bool die_already = false;

void *reader_thread(void *arg)
{
    my_message_b::queue_t * q = (my_message_b::queue_t *) arg;
    my_message_b::queue_t * qs[1] = { q };

    while (!die_already)
    {
        my_message_b * mb = NULL;
        my_message_d1 * md1 = NULL;
        my_message_d2 * md2 = NULL;
        int which_q = -1;
        printf("READER entering dequeue(2000)\n");

        mb = my_message_b::queue_t::dequeue_multi(
            1, qs, &which_q, 2000);
        if (mb)
        {
            printf("READER GOT MSG:\n");
            mb->print();

            md1 = dynamic_cast<my_message_d1*>(mb);
            if (md1)
                printf("dynamic cast to md1 is OK!\n");
            else
                printf("dynamic cast to md1 is NULL!\n");

            md2 = dynamic_cast<my_message_d2*>(mb);
            if (md2)
                printf("dynamic cast to md2 is OK!\n");
            else
                printf("dynamic cast to md2 is NULL!\n");

            mb->deref();
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
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);

    // prepopulate with 2 buffers, add 10 on grows.
    my_message_b::pool_t mypool(2,10,&mattr,&cattr);

    my_message_b::queue_t    q(&mattr,&cattr);

    pthread_mutexattr_destroy(&mattr);
    pthread_condattr_destroy(&cattr);

    my_message_b * mb;
    my_message_d1 * md1;
    my_message_d2 * md2;

    printstats(&mypool, "before first alloc");

    // this should succeed.
    printf("attempting first alloc, should succeed\n");
    if (my_message_b::get(&mb, &mypool,1, 1,2))
    {
        printf("enqueuing a message NOW\n");
        q.enqueue(mb);
    }
    else
        printf("FAILED\n");

    printstats(&mypool, "after first alloc");

    printf("attempting second alloc\n");
    if (my_message_d1::get(&md1, &mypool, 1000, 3,4,5,6))
    {
        printf("enqueuing a message NOW\n");
        q.enqueue(md1);
    }
    else
        printf("FAILED\n");

    printstats(&mypool, "after second alloc");


    // now that two enqueues have been done, start the reader.
    pthread_t id;
    pthread_attr_t       attr;
    pthread_attr_init   (&attr);
    pthread_create(&id, &attr, &reader_thread, &q);
    pthread_attr_destroy(&attr);




    // this should succeed.
    printf("attempting third alloc\n");
    // NOTE using my_message_b:: instead of my_message_d2:: !
    if (my_message_b::get(&md2,&mypool,T2T_GROW, 7,8,9,10,11))
    {
        printf("enqueuing a message NOW\n");
        q.enqueue(md2);
    }
    else
        printf("FAILED\n");

    printstats(&mypool, "after third alloc");

    printf("sleeping 3\n");
    sleep(3);
    printf("done sleeping 3\n");

    printf("telling READER to die and waiting\n");
    die_already =true;
    pthread_join(id, NULL);
    printf("READER is DEAD\n");

    return 0;
}
