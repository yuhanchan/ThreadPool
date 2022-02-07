#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    // Constructor declaration
    ThreadPool(size_t);

    template<class F, class... Args> // class... Ellipsis in C++ matches variable number of arguments

    // https://stackoverflow.com/questions/22514855/arrow-operator-in-function-heading
    // There are two ways to do function declaration:
    //      A more often seen: return-type identifier(arg-declarations)
    //      What is used here: auto identifier(arg-declaration) -> return-type
    //
    //      So the function below can also be declared as:
    //      std::future<typename std::result_of<F(Args...)>::type> enqueue(F&& f, Args&&... args);
    //
    //      Here it used second format probably because it's easier to read.
    //
    // std::result_of obtains the result type of a function call with arguments
    // the result type is aliased as member type result_of::type
    //
    // A future is an object that can retrieve a value from some provider object or function
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;

    // Destructor declaration
    ~ThreadPool();

private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue
    std::queue< std::function<void()> > tasks;
    
    // synchronization
    std::mutex queue_mutex; // mutex offers exclusive ownership to a thread, used to protect shared datafrom being simutaneously accessed by mutiple threads.
    std::condition_variable condition; // a synchronization primative used to block a thread or multiple threads, until a thread notified the condition.
    bool stop;
};
 
// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    :   stop(false)
{
    // This create #threads tasks, keep waiting and executing tasks until the queue is depleted
    for(size_t i = 0;i<threads;++i)
        workers.emplace_back(
            // A lambda funtion:
            // format: [captures] (params) mutable throw() -> return-type {body}
            //         mutable, throw(), and -> return type are optional
            [this]
            {
                for(;;)
                {
                    // create a task
                    std::function<void()> task;

                    // The mutex and lock is only used when retrieving task from queue, this is to prevent that muitple thread retrieve the same task. Once a task is retrieved, the lock is released, and the execution of tasks are parallel among threads.
                    {
                        // unique_lock is a general purpose mutex ownership wrapper, can be used with condition variables, which is the case here.
                        std::unique_lock<std::mutex> lock(this->queue_mutex); 

                        // syntax: std::condition_variable::wait(std::unique_lock<std::mutex> lock, Predicate stop_waiting);
                        this->condition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty(); }); // blocks the current thread until the condition is woken up. Stop waiting if this->stop is set or task queue is empty.

                        // check if both this->stop is set and task queue is empty
                        if(this->stop && this->tasks.empty())
                            return;
                        
                        // get the task from the front of task queue, and pop it.
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    } // I would guess that the lock will be automatically released here, since I don't see code that explicityly release the lock.

                    // execute the task in parallel
                    task();
                }
            }
        );
}

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    // aliasing return type
    using return_type = typename std::result_of<F(Args...)>::type;

    // std::make_shared create a std::shared_ptr object
    // std::packaged_task wraps any callable target, can be accessed through std::future object
    // std::bind(<F&& f, Args&& args) generates a forwarding wrapper for f, calling this wrapper is equiv to calling f with args.
    auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
    std::future<return_type> res = task->get_future();
    {
        // get lock before enqueue
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}

#endif
