// 封装 互斥锁、条件变量、信号量
#ifndef _SYNC_H
#define _SYNC_H
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

class Locker
{
public:
    Locker() {pthread_mutex_init(&lock_, NULL);}

    ~Locker()
    {
        pthread_mutex_lock(&lock_);
        pthread_mutex_destroy(&lock_);
    }

    void lock() {pthread_mutex_lock(&lock_);}

    void unlock() {pthread_mutex_unlock(&lock_);}

    pthread_mutex_t *get() {return &lock_;}

private:
    pthread_mutex_t lock_;
};


class Conditon
{
public:
    explicit Conditon(Locker &lock): lock_(lock) {pthread_cond_init(&cond_, NULL);}

    ~Conditon() {pthread_cond_destroy(&cond_);}

    void wait() {pthread_cond_wait(&cond_, lock_.get());}

    void waitForSeconds(double seconds)
    {
        timespec abstime;
        clock_gettime(CLOCK_REALTIME, &abstime);

        const int64_t kNanoSecondsPerSecond = 1000000000;
        int64_t nanoseconds = static_cast<int64_t>(seconds * kNanoSecondsPerSecond);

        abstime.tv_sec += static_cast<time_t>((abstime.tv_nsec + nanoseconds) / kNanoSecondsPerSecond);
        abstime.tv_nsec = static_cast<long>((abstime.tv_nsec + nanoseconds) % kNanoSecondsPerSecond);
        pthread_cond_timedwait(&cond_, lock_.get(), &abstime);

    }

    void signal() {pthread_cond_signal(&cond_);}

    void broadcast() {pthread_cond_broadcast(&cond_);}

private:
    pthread_cond_t cond_;
    Locker &lock_;    // 一定要用引用，否则是不一样的互斥锁
};


class Sem
{
public:
    Sem(int n = 0) {sem_init(&sem_, 0, n);}

    void wait() {sem_wait(&sem_);}

    void post() {sem_post(&sem_);}
private:
    sem_t sem_;
};


#endif