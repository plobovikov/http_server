#ifndef THREAD_H
#define THREAD_H

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <memory>

namespace m_thread {
class condition_variable;
class mutex{

public:

    enum mtx_type{Normal,Recursive};

private:

    pthread_mutex_t mtx;

public:

    mutex(mtx_type type){
        static std::map<mtx_type,std::function<void()>>init_map = {
                std::make_pair(Normal,[&](){
            pthread_mutex_init(&mtx,NULL);
        }),
                std::make_pair(Recursive,[&](){
            pthread_mutexattr_t attr;
            pthread_mutexattr_init(&attr);
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init(&mtx, &attr);
        })
    };
        init_map[type]();
    }
    void lock(){
        pthread_mutex_lock(&mtx);
    }
    void unlock(){
        pthread_mutex_unlock(&mtx);
    }
    bool try_lock(){
        return (!pthread_mutex_trylock(&mtx) ? true : false);
    }
    ~mutex(){
        pthread_mutex_destroy(&mtx);
    }
    friend class condition_variable;
};
template<class Mutex>
class lock_guard{

    mutex *mtx;

public:

    lock_guard(Mutex *mutex){
        mtx = mutex;
        mtx->lock();
    }
    ~lock_guard(){
        mtx->unlock();
    }
};
class condition_variable{

    pthread_cond_t cv;

public:

    condition_variable(){
        pthread_cond_init(&cv, NULL);
    }
    ~condition_variable(){
        pthread_cond_destroy(&cv);
    }
    template <class Mutex>
    void wait(Mutex &mtx)
    {
        pthread_cond_wait(&cv,&mtx.mtx);
    }
    void notify_one()
    {
        pthread_cond_signal(&cv);
    }
    void notify_all()
    {
        pthread_cond_broadcast(&cv);
    }

};

struct implementaion_base
{
    inline virtual ~implementaion_base() = default;
    virtual void run() = 0;
};
template<typename Callable>
struct implementation : public implementaion_base{
    Callable _f;
    implementation(Callable &&_f) : _f(std::forward<Callable>(_f)){

    }
    void run() { _f(); }

};

class thread{

    std::shared_ptr<implementaion_base>_f;
    pthread_t t;
    mutable mutex mtx;
    bool isActive;
    pthread_attr_t t_attr;

    template<typename Callable>
    std::shared_ptr<implementation<Callable>> make_routine(Callable &&_f){
        return std::make_shared<implementation<Callable>>(std::forward<Callable>(_f));
    }

public:
    enum thread_type{Joinable,Detached};

    static void* wrapper(void *arg){
        ((implementaion_base*)arg)->run();
        return NULL;
    }
    template<typename Callable>
    thread(thread_type type,Callable _f) : _f(make_routine(_f)), mtx(mutex::Normal) {
        if(type == Detached){
            pthread_attr_init(&t_attr);
            pthread_attr_setdetachstate(&t_attr, PTHREAD_CREATE_DETACHED);
            isActive = false;
        }
        else
            isActive = true;
        pthread_create(&t,NULL,wrapper,(void*)this->_f.get());
    }
    template<typename Callable,typename... Args>
    thread(thread_type type,Callable &&_f,Args&&... _args) :
        _f(make_routine(std::bind(_f,_args...))),mtx(mutex::Normal){
        if(type == Detached){
            pthread_attr_t t_attr;
            pthread_attr_init(&t_attr);
            pthread_attr_setdetachstate(&t_attr, PTHREAD_CREATE_DETACHED);
            isActive = false;
        }
        else
            isActive = true;
        pthread_create(&t,NULL,wrapper,(void*)this->_f.get());
    }
    ~thread(){}
    void join(){
        if(joinable())
            pthread_join(t, NULL);
    }
    bool joinable()const{
        mtx.lock();
        bool result = isActive;
        mtx.unlock();
        return result;
    }
    long get()const{return t;}

};
}
#endif // THREAD_H
