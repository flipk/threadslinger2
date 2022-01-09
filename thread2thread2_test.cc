#if 0
set -e -x

incs=""
srcs="thread2thread2.cc thread2thread2_test.cc"
libs="-lpthread"
defs=""
cflags="-O3"
lflags=""
std="-std=c++11"

clear
rm -f t 0log 0compilelog
script 0compilelog -c "g++ -fdiagnostics-color=always $std $cflags $lflags $defs $incs $srcs $libs  -o t"

if [[ "x$1" = "x" ]] ; then
    if [[ -x t ]] ; then
       script 0log -c ./t
    fi
fi
exit 0
;
#endif

#include "thread2thread2.h"
#include <sys/types.h>
#include <signal.h>
#include <string.h>

using namespace std;

// neat, but not using them just now:
//template <class... T> using t2mb = t2t2::t2t2_message_base<T...>;
//template <class    T> using t2sp = t2t2::t2t2_shared_ptr  <T>;
namespace t2t2 = Thread2Thread2;

class my_message_base : public t2t2::t2t2_message_base<my_message_base>
{
public:
    // convenience
    typedef t2t2::t2t2_queue<my_message_base> queue_t;
    typedef t2t2::t2t2_queue_set<my_message_base> queue_set_t;
    typedef t2t2::t2t2_shared_ptr<my_message_base> sp_t;

    enum msgtype { TYPE_B, TYPE_D1, TYPE_D2 } type;
    int a;
    int b;
    my_message_base(int _a, int _b)
        : type(TYPE_B), a(_a), b(_b)
    {
        printf("constructing my_message_base type %d a=%d b=%d\n", type,a,b);
    }
    my_message_base(msgtype _t, int _a, int _b)
        : type(_t), a(_a), b(_b)
    {
        printf("constructing my_message_base type %d a=%d b=%d\n", type,a,b);
    }
    virtual ~my_message_base(void)
    {
        printf("destructing my_message_base type %d a=%d b=%d\n", type,a,b);
    }
    virtual void print(void) const
    {
        printf("virtual print: "
               "message type is my_message_base, a=%d b=%d\n", a, b);
    }
};

class my_data : public t2t2::t2t2_message_base<my_data>
{
public:
    // convenience
    typedef t2t2::t2t2_pool<my_data> pool_t;
    typedef t2t2::t2t2_shared_ptr<my_data> sp_t;

    int len;
    char buf[1000];
    my_data(void)
    {
        printf("my_data constructor\n");
        len = 0;
        memset(buf,0,sizeof(buf));
    }
    ~my_data(void)
    {
        printf("my_data destructor\n");
    }
};

class my_message_derived1 : public my_message_base
{
public:
    // convenience
    typedef t2t2::t2t2_pool<my_message_base,
                          my_message_derived1> pool1_t;
    typedef t2t2::t2t2_shared_ptr<my_message_derived1> sp_t;

    int c;
    int d;
    my_data::sp_t  data;

    my_message_derived1(int _a, int _b, int _c, int _d)
        : my_message_base(TYPE_D1, _a, _b), c(_c), d(_d)
    {
        printf("constructing my_message_derived1 c=%d d=%d\n", c,d);
    }
    virtual ~my_message_derived1(void)
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
    typedef t2t2::t2t2_pool<my_message_base,
                          my_message_derived2> pool2_t;
    typedef t2t2::t2t2_shared_ptr<my_message_derived2> sp_t;

    // test hack: if e==-1, that tells recipient to exit.
    int e;
    int f;
    int g;
    my_message_derived2(int _a, int _b, int _e, int _f, int _g)
        : my_message_base(TYPE_D2, _a, _b), e(_e), f(_f), g(_g)
    {
        printf("constructing my_message_derived2 e=%d f=%d g=%d\n", e,f,g);
    }
    virtual ~my_message_derived2(void)
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
void printstats(t2t2::t2t2_pool<BaseT,derivedTs...> *pool,
                const char *what)
{
    t2t2::t2t2_pool_stats  stats;
    pool->get_stats(stats);
    cout << what << ": " << stats << endl;
}

static void
my_t2t2_assert_handler(t2t2::t2t2_error_t e, bool fatal,
                      const char *filename, int lineno)
{
    fprintf(stderr,
            "\n\nERROR: Thread2Thread2 ASSERTION %d (%s) at %s:%d\n\n",
            e, t2t2::t2t2_error_types[(int)e], filename, lineno);
    // i want a core dump that i can gdb
    kill(0, SIGABRT);
}

void *reader_thread(void *arg);

int main(int argc, char ** argv)
{
    t2t2::t2t2_assert_handler = &my_t2t2_assert_handler;

    pthread_mutexattr_t  mattr;
    pthread_condattr_t   cattr;

    pthread_mutexattr_init(&mattr);
    pthread_condattr_init(&cattr);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);

    my_message_derived1::pool1_t   mypool1( 1,10,&mattr,&cattr);
    my_message_derived2::pool2_t   mypool2( 1,10,&mattr,&cattr);
    my_message_base    ::queue_t  myqueue1(      &mattr,&cattr);
    my_message_base    ::queue_t  myqueue2(      &mattr,&cattr);
    my_message_base::queue_set_t      qset(      &mattr,&cattr);
    my_data::pool_t               datapool(20,20,&mattr,&cattr);

    qset.add_queue(&myqueue1, 1);
    qset.add_queue(&myqueue2, 2);

    pthread_mutexattr_destroy(&mattr);
    pthread_condattr_destroy(&cattr);

    printstats(&mypool1, "1");
    printstats(&mypool2, "2");
    printstats(&datapool, "data");

    printf("queue 1 is currently %s\n",
           myqueue1.empty() ? "EMPTY" : "NOT EMPTY");

    printf("attempting first alloc\n");
    my_message_base::sp_t  spmb;
    if (mypool1.alloc(&spmb,
                      t2t2::T2T2_NO_WAIT,
                      1,2))
    {
        printf("enqueuing a message NOW\n");
        if (myqueue1.enqueue(spmb) == false)
            printf("ENQUEUE RETURNED FALSE\n");
    }
    else
        printf("ALLOC FAILED\n");

    printf("queue 1 is currently %s\n",
           myqueue1.empty() ? "EMPTY" : "NOT EMPTY");

    printstats(&mypool1, "1");
    printstats(&mypool2, "2");
    printstats(&datapool, "data");

    printf("attempting second alloc\n");
    my_message_derived1::sp_t  spmd1;
    if (mypool1.alloc(&spmd1,
                     t2t2::T2T2_GROW,
                     3,4,5,6))
    {
        datapool.alloc(&spmd1->data, 1000);

        printf("enqueuing a message NOW\n");
        if (myqueue2.enqueue(spmd1) == false)
            printf("ENQUEUE RETURNED FALSE\n");
    }
    else
        printf("ALLOC FAILED\n");

    printstats(&mypool1, "1");
    printstats(&mypool2, "2");
    printstats(&datapool, "data");

    printf("attempting third alloc\n");
    my_message_derived2::sp_t  spmd2;
    if (mypool2.alloc(&spmd2,
                      1000,
                     7,8,9,10,11))
    {
        printf("enqueuing a message NOW\n");
        if (myqueue2.enqueue(spmd2) == false)
            printf("ENQUEUE RETURNED FALSE\n");
    }
    else
        printf("FAILED\n");

    printstats(&mypool1, "1");
    printstats(&mypool2, "2");
    printstats(&datapool, "data");

    printf("\nnow starting reader thread:\n");

    // now that three enqueues have been done, start the reader.

    pthread_attr_t       attr;
    pthread_attr_init   (&attr);
    pthread_t       id;
    pthread_create(&id, &attr, &reader_thread, &qset);
    pthread_attr_destroy(&attr);

    // the reader should now dequeue the backlog we put
    // in above (in the proper priority order) and then
    // it should hit its timeout case a couple of times.
    sleep(1);


    printf("attempting fourth alloc for EXIT message\n");
    if (mypool2.alloc(&spmd2,
                      1000,
                      12,13,-1,14,15)) // e=-1 means exit!
    {
        printf("enqueuing EXIT message NOW\n");
        if (myqueue1.enqueue(spmd2) == false)
            printf("ENQUEUE RETURNED FALSE\n");
    }
    else
        printf("FAILED\n");


    // now that we've sent the EXIT message, the thread
    // should be exiting soon, so wait for it, and then
    // we can shut down gracefully.
    pthread_join(id, NULL);

    printstats(&mypool1, "1");
    printstats(&mypool2, "2");
    printstats(&datapool, "data");

    return 0;
}

void *reader_thread(void *arg)
{
    my_message_base::queue_set_t *qset =
        (my_message_base::queue_set_t *) arg;

    bool keep_going = true;
    while (keep_going)
    {
        my_message_base::sp_t mb;
        int qid = -1;

        printf("READER entering dequeue()\n");

        mb = qset->dequeue(250, &qid);

        if (mb)
        {
            printf("READER GOT MSG on queue %d:\n", qid);
            mb->print();

            switch (mb->type)
            {
            case my_message_base::TYPE_B:
            {
                printf("type is my_message_base\n");
                break;
            }
            case my_message_base::TYPE_D1:
            {
                my_message_derived1::sp_t  md1;
                // this one tests the casting operator=
                if (md1 = mb)
                    printf("dynamic cast to md1 is OK! c,d = %d,%d\n",
                           md1->c, md1->d);
                else
                    printf("dynamic cast to md1 FAILED\n");
                break;
            }
            case my_message_base::TYPE_D2:
            {
                // this one tests the casting constructor
                my_message_derived2::sp_t  md2 = mb;
                if (md2)
                {
                    printf("dynamic cast to md2 is OK! e,f,g = %d,%d,%d\n",
                           md2->e, md2->f, md2->g);
                    if (md2->e == -1)
                    {
                        printf("This is an EXIT message!\n");
                        keep_going = false;
                    }
                }
                else
                    printf("dynamic cast to md2 FAILED\n");
                break;
            }
            default:
                printf("unknown message type %u\n", mb->type);
            }
        }
        else
            printf("READER GOT NULL\n");
    }

    return NULL;
}
