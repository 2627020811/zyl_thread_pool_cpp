#pragma once

#include <functional>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <list>
#include <cstdint>
#include <semaphore>
#include <shared_mutex>
#include <future>

/**
    @namespace thread_pool utils
    @brief A namespace that contain classes and functions related to thread utilities.
**/

namespace thread_utils
{
    /**
        *@class thread pool
        *@brief Represents a thread pool for executing tasks concurrently.

        *the thread class provides a simple interface for executing tasks using a pool of worker threads.
        *the class allows submitting tasks to the thread pool, pausing and resuming the pool, and shutdown the pool
    **/
    class thread_pool
    {
    private:
        class worker_thread; // 工作线程类
        enum class status_p : std::int8_t
        {
            TERMINATED = -1,
            TERMINATING = 0,
            RUNNING = 1,
            PAUSED = 2,
            SHUTDOWN = 3
        };
        // 线程池的状态：-1：线程池已终止，0：线程池将终止，1：线程池正在运行，2：线程池被暂停
        // 3：线程池在等待任务完成，但不再接受新任务
        std::atomic<status_p> status;            // std::atomic<T> 是 C++ 标准库中提供的用于原子操作的模板类，用于确保对变量的读取和写入是原子的，即不会被中断或交错执行。在多线程编程中，使用原子类型可以避免竞态条件和数据竞争问题。
        std::atomic<std::size_t> max_task_count; // 任务队列中的任务的最大数量，0表示没有限制
        // 当任务队列中任务数量超过这个值时，新提交的任务会被拒绝
        std::shared_mutex pool_status_mutex;                  // 状态变量读写锁
        std::shared_mutex task_queue_mutex;              // 任务队列的互斥锁
        std::shared_mutex worker_list_mutex;             // 线程列表的互斥锁
        std::condition_variable_any task_queue_cv;       // 任务队列的条件变量
        std::condition_variable_any task_queue_empty_cv; // 任务队列为空的条件变量
        std::queue<std::function<void()>> task_queue;    // 任务队列，其中存储着待执行的任务
        // std::function<void()>: 是 C++11 引入的通用可调用对象封装，可以包裹任何可调用对象（如函数、Lambda 表达式、函数对象等），并提供一致的调用接口。

        std::list<worker_thread> worker_list; // 线程列表，其中存储着工作线程
        // 禁用拷贝构造函数、移动构造函数、赋值运算符
        thread_pool(const thread_pool &) = delete;
        thread_pool(thread_pool &&) = delete;
        thread_pool &operator=(const thread_pool &) = delete;
        thread_pool &operator=(thread_pool &&) = delete;

        // 在取得对状态变量的独占访问后，调用以下函数，以确保线程池的状态变更是原子的
        void pause_with_pool_status_lock();
        void resume_with_pool_status_lock();
        void terminate_with_pool_status_lock();
        void wait_with_pool_status_lock();
        void shutdown_with_pool_status_lock();
        void shutdown_now_with_pool_status_lock();

    public:
        // 不同平台的size_t会用不同的类型实现，使用size_t而非int或unsigned可以写出扩展行更好的代码。
        thread_pool(std::size_t initial_thread_count, std::size_t max_task_count = 0); // 构造函数
        ~thread_pool();
        // 模板函数，这个模板的目的是定义一个函数模板，它可以接受任意类型的可调用对象 F（比如函数、函数指针、lambda 表达式等），以及任意数量和类型的参数 Args...。
        template <typename F, typename... Args>
        auto submit(F &&func, Args &&...args) -> std::future<decltype(func(args...))>;
        // submit 是一个模板函数声明，这段代码是一个函数模板声明，用于创建一个异步任务并返回与任务关联的 std::future 对象，可以获取任务的返回值或异常。
        // 它接受一个函数对象 F func（例如 print_value）和一系列额外的参数 Args...（例如 42）。然后，它将传递的函数对象和参数传递给 func(args...)，从而实现对函数对象的调用。
        //-> std::future<decltype(f(args...))>：这是函数模板的返回类型声明，它表明该函数返回一个 std::future 对象
        // decltype(f(args...)) 用于获取函数对象 f 调用后的返回类型并将其用作 std::future 的模板参数

        void pause();
        void resume();
        void shutdown();     // 等待所有任务完成后再终止线程池
        void shutdown_now(); // 立即终止线程池，会丢弃任务队列中的任务
        void terminate();    // 终止线程池
        void wait();         // 等待所有任务执行完毕
        void add_thread(std::size_t count_to_add);
        void remove_thread(std::size_t count_to_remove);
        void set_max_task_count(std::size_t count_to_set);
        std::size_t get_thread_count();
        std::size_t get_task_count();
    };
    /**
     * @class thread_pool::worker_thread
     * @brief represents a worker thread in the thread pool.
     *
     * The worker_thread class represent a worker thread in the thread pool
     * each worker thread is responsible for excuting tasks submitted to the thread pool.
     * **/

    class thread_pool::worker_thread
    {
    private:
        enum class status_t : std::int8_t
        {
            TERMINATED = -1,
            TERMINATING = 0,
            RUNNING = 1,
            PAUSED = 2,
            BLOCKED = 3
        }; // 状态变量类型，-1：线程已终止，0：线程将终止，1：线程正在运行，2：线程被暂停，3：线程在阻塞等待新任务
        std::atomic<status_t> status;
        // 声明了一个 std::atomic 对象 status，用于原子操作 status_t 类型的变量。std::atomic 提供了原子操作，用于在多线程环境中对共享变量进行安全访问，避免竞态条件和数据竞争问题。
        std::binary_semaphore pause_sem; // 信号量，用于线程暂停时的阻塞

        std::shared_mutex thread_status_mutex; // 状态变量的互斥锁
        // shared_mutex同时支持共享/独占互斥锁，共享用shared_lock管理，独占用unique_lock
        thread_pool *pool;              // 从属的线程池
        std::thread thread;             // 工作线程
        // 禁用拷贝构造函数/移动构造函数及赋值运算符、移动赋值运算符
        worker_thread(const worker_thread &) = delete;
        worker_thread(worker_thread &&) = delete;
        worker_thread &operator=(const worker_thread &) = delete;
        worker_thread &operator=(worker_thread &&) = delete;
        // 在取得对状态变量的独占访问后，调用以下函数，以确保线程的状态变更是原子的
        status_t terminate_with_thread_status_lock();
        void pause_with_thread_status_lock();
        void resume_with_thread_status_lock();

    public:
        worker_thread(thread_pool *pool);
        ~worker_thread();
        status_t terminate();
        void pause();
        void resume();
    };
    // inline/template function implementations
    // thread_pool

    /**
     * Submits a task to the thread pool for execution.
     *
     * this function submits a task to the thread pool for execution. The task is callable
     * object that tasks argument specified by the template parameters. The function returns a std::future object
     * that can be used to retrieve the result of the task once it has completed
     *
     * @tparam F The type of the callable object
     * @tparam Args The type of argument to the callable object
     * @param f The callable object to be executed
     * @param args The arguments to be passed to the callbale object.
     * @return A std::future object representing the result of the task
     * @throws std::runtime_error if the thread pool is in an invalid state or the task queue is full**/

    template <typename F, typename... Args>
    // submit(F &&func, Args &&...args) -> std::future<decltype(f(args...))>
    auto thread_pool::submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))>
    {
        // 提交任务
        std::shared_lock<std::shared_mutex> status_lock(pool_status_mutex); // status_mutex是锁，status_lock是管理锁的的操作的
        // 使用了 std::atomic<status_t> 类型的 status 变量，通过 load() 方法加载当前的线程池状态，并根据状态进行相应的处理。
        switch (status.load()) //
        {
        case status_p::TERMINATED: // 线程池已终止
            throw std::runtime_error("[thread_pool::submit][error]:thread pool terminated");
        case status_p::TERMINATING: // 线程池将终止
            throw std::runtime_error("[thread_pool::submit][error]:thread pool is terminating");
        case status_p::PAUSED: // 线程池将暂停
            throw std::runtime_error("[thread_pool::submit][error]:thread pool is paused");
        case status_p::SHUTDOWN: // 线程池在等待任务完成，且不再接受新任务
            throw std::runtime_error("[thread_pool::submit][error]:thread pool is waiting for tasks to complete,\
            but not accepting new tasks");
        case status_p::RUNNING: // 线程池正在运行
            break;
        default:
            throw std::runtime_error("[thread_pool::submit][error]:unknown status");
        }

        if (max_task_count > 0 && get_task_count() >= max_task_count)
        { // 如果任务队列已满，则拒绝提交任务
            throw std::runtime_error("[thread_pool::submit][error]:task queue is full");
        }
        using return_type = decltype(f(args...)); // 返回类型别名
        // make_shared是创建shared_ptr，用法：std::shared_ptr<T> make_shared<T>( Args&&... args );
        auto task = std::make_shared<std::packaged_task<return_type>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...))
            // 这段代码是一个使用 std::packaged_task 和 std::bind 的实现，用于封装一个可调用对象（如函数或函数对象）和其参数，以便在异步任务中执行。
            // std::packaged_task 是 C++11 中引入的一个工具，用于封装可调用对象（函数、函数对象或 Lambda 表达式）的异步任务，并将任务的结果传递给 std::future 以供后续获取。

            // #include <iostream>
            //  #include <future>
            //  #include <functional>

            // int main() {
            //     // 创建一个 packaged_task，用于计算两个整数的和
            //     std::packaged_task<int(int, int)> task([](int a, int b) { return a + b; });

            //     // 获取与任务结果关联的 future 对象
            //     std::future<int> result = task.get_future();

            //     // 执行任务，传递参数 2 和 3
            //     task(2, 3);

            //     // 获取异步任务的结果
            //     int sum = result.get();

            //     std::cout << "Sum: " << sum << std::endl;

            //     return 0;
            // }

            // 模板参数 return_type:return_type 是一个模板参数，代表封装的任务（可调用对象）的返回类型。它可以是任何你期望任务执行完后返回的类型，比如 int、double、std::string 或者任何自定义类型。
            // std::packaged_task<return_type> 的作用:std::packaged_task<return_type> 封装了一个任务，并将任务的结果绑定到一个 std::future<return_type> 对象上，允许异步获取任务的结果。当任务执行完成后，可以通过 std::future<return_type> 获取任务的返回值。
            // std::bind 是一个函数模板，用于将可调用对象和其参数绑定在一起，返回一个新的可调用对象。在这里，f 是可调用对象（函数或函数对象），args... 是其参数。通过 std::forward 将 f 和 args... 转发给 std::bind，以保留参数的完美转发特性。
            // std::make_shared 是用于创建 std::shared_ptr 对象的工厂函数，用于动态分配和构造一个 std::packaged_task<return_type> 对象，并返回其指针。
            // 我的理解：std::packaged_task<return_type>是一个可调用对象的封装，它的参数也就是可调用对象我用bind来生成，并创造智能指针指向，作为task的表示

            std::future<return_type> res=task->get_future();// 获取与任务结果关联的 future 对象

            std::unique_lock<std::shared_mutex> lock(task_queue_mutex); //排他锁（独占锁）,unique_lock是管理shared_mutex的
            task_queue.emplace([task](){(*task)()});//将任务队列封装为一个lambda表达式并放入任务队列
            //task_queue: 这是一个队列，存储的元素类型是 std::function<void()>，表示可以存放任意可调用对象（函数、lambda 函数、函数对象等）。
            lock.unlock();//解锁,unique_lock离开作用域调用析构函数时会自动解锁，但是也可以手动在某一时刻解锁
            task_queue_cv.notify_one();//唤醒一个等待队列中的线程，notify_all()是唤醒所有
            return res;
    }

    inline void thread_pool::set_max_task_count(std::size_t count_to_set)
    {
        //设置任务队列中任务的最大数量，如果设置后的最大数量小于当前任务数量，则会拒绝提交新的任务，直到任务数量小于等于最大数量
        max_task_count.store(count_to_set);
        // store(count_to_set) 将 count_to_set 的值存储到 max_task_count 中。这个操作会将 max_task_count 的值原子地设置为 count_to_set。
    }
}// namespace thread_utils
