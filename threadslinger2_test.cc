#if 0
set -e -x

incs="-I../libpfkutil"
srcs="threadslinger2.cc threadslinger2_test.cc"
libs="../libpfkutil/signal_backtrace.cc ../libpfkutil/dll3.cc -lpthread"
defs=""
cflags="-O0 -g3"
lflags=""

g++ $cflags $lflags $defs $incs $srcs $libs  -o t
./t
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

int main(int argc, char ** argv)
{
    // create a pool of my_message, prepopulated
    // with 1 message, and if grow=true, grows ten at a time.
    my_message::pool_t * mypool =
        new my_message::pool_t(1,10,NULL,NULL,CLOCK_REALTIME);

    {
        my_message::up  m1;
        my_message * m;

        printstats(mypool, "before first alloc");

        // this should succeed.
        m = new(mypool,0,false) my_message(1,2);
        if (m)
            m1.reset(m);

        printstats(mypool, "after first alloc");

        // this should return NULL after a 1s pause.
        m = new(mypool,1000,false) my_message(3,4);
        if (m)
            m1.reset(m);

        printstats(mypool, "after second alloc");

        // this should succeed.
        m = new(mypool,0,true) my_message(5,6);
        if (m)
            // this should cause the 1,2 message
            // to be destroyed and freed back to pool.
            m1.reset(m);

        printstats(mypool, "after third alloc");

    } // here, the 5,6 message should be destroyed and freed.

    printstats(mypool, "after final destroy");

    delete mypool;

    return 0;
} // here, the containers in the pool should be freed.
