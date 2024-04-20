#include "include/thread_pool.hpp"

namespace thread_utils
{
    /**
     * @brief Constructs a thread pool object.
     *
     * This constructor initializes a thread pool object with the specified initial thread count and maximum task count.
     * @param initial_thread_count The initial number of threads in the thred pool
     * @param max_task_count The maximum number of tasks that can be queued in the thread pool.
     */

    thread_pool::thread_pool(std::size_t initial_thread_count, std::size_t max_task_count)
        : max_task_count(max_task_count), status(status_p::RUNNING)
    {
        for (std::size_t i = 0; i < initial_thread_count; i++)
        {
            // 创建线程并插入线程列表
            worker_list.emplace_back(this); // 隐式调用worker_thread::worker_thread(thread_pool* pool)构造函数
        }
    }

    thread_pool::~thread_pool()
    {
        terminate(); // 析构时终止线程池
    }

    /**
     * Pauses the thread pool by setting its status to PAUSED ans pausing all worker threads.
     * If the thread pool is already in a paused state or terminated state,no further action is taken.
     *
     * @throws std::runtime_error if the thread pool is in a unknown status.
     */
    void thread_pool::pause_with_pool_status_lock()
    {
        switch (status.load())
        {
        case status_p::TERMINATED:  // 线程池已终止
        case status_p::TERMINATING: // 线程池将终止
        case status_p::PAUSED:      // 线程池将暂停
        case status_p::SHUTDOWN:    // 线程池在等待任务完成，但不再接受新任务
            return;
        case status_p::RUNNING:
            status.store(status_p::PAUSED);
        default:
            throw std::runtime_error("[thread_pool::pause][error]:unknown status");
        }
        std::unique_lock<std::shared_mutex> lock(worker_list_mutex); // 线程队列是临界资源
        for (auto &worker : worker_list)
        {
            // 暂停所有线程
            worker.pause();
        }
    }

    /**
     * Resumes the thread pool by changing its status to RUNNING and resumes all worker threads;
     * If the thread pool is already in a terminated,teminating,or running state, this function does nothing.
     * If the thread pool is paused, it changes the status to RUNNING and resumes all worker threads.
     * If the thread pool is in a shutdown state, it does nothing.
     * If the thread pool is in a unknown status, it throws a std::runtime_error.
     *
     * @throws std::runtime_error if the thread pool is in an unknown state.
     */

    void thread_pool::resume_with_pool_status_lock() // 从PAUSED到RUNNING
    {
        switch (status.load())
        {
        case status_p::TERMINATED:           // 线程池已终止
        case status_p::TERMINATING:          // 线程池将终止
        case status_p::RUNNING:              // 线程池正在运行
            return;                          // 在这些状态下，不需要恢复线程
        case status_p::PAUSED:               // 线程被暂停
            status.store(status_p::RUNNING); // 设置线程池的状态为运行
        case status_p::SHUTDOWN:             // 线程池在等待任务完成，但不再接受新任务
            break;
        default: // 未知状态
            throw std::runtime_error("[thread_pool::resume][error]:unknown status");
        }
        std::unique_lock<std::shared_mutex> lock(worker_list_mutex);// RAII,在构造时自动加锁,离开作用域时会自动解锁
        for (auto &worker : worker_list)
        {
            // 恢复所有线程
            worker.resume();
        }
    }
    /**
     * @brief Shuts down the thread pool and waits for all tasks to complete.
     *
     * This function sets the status of the thread pool to "SHUTDOWN" and waits for all tasks in the task queue to be completed.
     * Tf the thread pool is already terminated, terminating, or in the process of shuting down, this function does not thing
     * If the thread pool is paused, it resumes the execution so that tasks can continue to be execute and consume the tasks in the task queue.
     *
     * @throws std::runtime_error if the thread pool is in an unknown status.
     */

    void thread_pool::shutdown_with_pool_status_lock()
    {
        // 等待所有任务执行完毕后再终止线程池
        switch (status.load())
        {
        case status_p::TERMINATED:  // 线程池已终止
        case status_p::TERMINATING: // 线程池将终止
        case status_p::SHUTDOWN:    // 线程池在等待任务完成，但不再接受新的任务
            return;                 // 在这些状态下，不需要额外操作
        case status_p::PAUSED:
            resume_with_pool_status_lock(); // 恢复线程池运行，以便任务能够继续运行以消耗任务队列中规定任务
        case status_p::RUNNING:        // 线程池正在运行
            status.store(status_p::SHUTDOWN);
            break;
        default: // 未知状态
            throw std::runtime_error("[thread_pool::shutdown][error]:unknown status");
        }
        std::shared_lock<std::shared_mutex> lock(task_queue_mutex); // 任务线程可以读共享
        while (!task_queue.empty())
        {
            task_queue_empty_cv.wait(lock); // 子进程的中wait函数对互斥量进行解锁，同时线程进入阻塞或者等待状态。就是释放锁lock然后等待，这样做的目的是允许其他线程在此期间获取该锁，以便对共享资源进行操作。
        }
        terminate_with_pool_status_lock();
    }

    /**
     * waits for all tasks to complete while holding the status lock;
     *
     * This function waits for all tasks in the task queue to complete before returning.
     * It checks the current status of the thread pool and performs different action based on the status.
     * If the status is TERMINATED, the function returns immediately as there is no need to wait.
     * If the status is TERMINATING,PAUSED,SHUTDOWN,or RUNNING, the function continues to wait until the task queue is empty.
     * If the status is unknown, a std::runtime_error is unknown.
     */

    void thread_pool::wait_with_pool_status_lock()
    {
        // 等待所有任务完成
        switch (status.load())
        {
        case status_p::TERMINATED:
            return;
        case status_p::TERMINATING:
        case status_p::PAUSED:
        case status_p::SHUTDOWN:
        case status_p::RUNNING:
            break;
        default:
            throw std::runtime_error("[thread_pool::wait][error]:unknown status");
        }

        std::shared_lock<std::shared_mutex> lock(task_queue_mutex);
        while (!task_queue.empty())
        {
            // 等待任务队列为空
            task_queue_empty_cv.wait(lock);
        }
    }

    void thread_pool::wait()
    {
        // 等待所有任务执行完毕
        std::shared_lock<std::shared_mutex> lock(pool_status_mutex);
        wait_with_pool_status_lock();
    }

    /**
     * @brief Terminates the thread pool with lock on the status.
     *
     * this function is responsible for terminating the thread pool by setting the status to TERMINATING,
     * terminating all worker thread, notifing any blocked threads waitting for tasks, and setting the status
     * to TERMINATED once all operations are completed.
     *
     * @throws std::runtime_error if the status is unknown.
     */

    void thread_pool::terminate_with_pool_status_lock()
    {
        switch (status.load())
        {
        case status_p::TERMINATED:  // 线程池已经终止，不需要终止了
            return;                 // 在这种状态下，不需要进一步操作
        case status_p::TERMINATING: // 线程池将终止
            break;
        case status_p::RUNNING:                  // 线程池正在运行
        case status_p::PAUSED:                   // 线程池被暂停
        case status_p::SHUTDOWN:                 // 线程池在等待任务完成，但不再接受新的任务
            status.store(status_p::TERMINATING); // 设置线程池的状态为将终止
            break;
        default:
            throw std::runtime_error("[thread_pool::terminate][error]:unknown status");
        }

        std::unique_lock<std::shared_mutex> lock(worker_list_mutex);
        for (auto &worker : worker_list)
        {
            // 终止所有线程
            worker.terminate();
        }
        task_queue_cv.notify_all(); // 唤醒所有等待任务的阻塞线程
        status.store(status_p::TERMINATED);
    }

    void thread_pool::shutdown()
    {
        // 等待所有任务执行完毕后再终止线程池
        std::unique_lock<std::shared_mutex> lock(pool_status_mutex);
        shutdown_with_pool_status_lock();
    }

    inline void thread_pool::shutdown_now_with_pool_status_lock()
    {
        // 立即终止线程池
        terminate_with_pool_status_lock();
    }

    void thread_pool::shutdown_now()
    {
        // 立即终止线程池
        std::unique_lock<std::shared_mutex> lock(pool_status_mutex);
        shutdown_now_with_pool_status_lock();
    }

    void thread_pool::pause()
    {
        // 暂停线程池
        std::unique_lock<std::shared_mutex> lock(pool_status_mutex);
        pause_with_pool_status_lock();
    }

    void thread_pool::resume()
    {
        // 恢复线程池
        std::unique_lock<std::shared_mutex> lock(pool_status_mutex);
        resume_with_pool_status_lock();
    }

    void thread_pool::terminate()
    {
        // 终止线程池
        std::unique_lock<std::shared_mutex> lock(pool_status_mutex);
        terminate_with_pool_status_lock();
    }

    /**
     * @brief Add threads to the thread pool.
     *
     * This function adds the specified number of threads to the thread pool.
     * The threads are created and added to the worker list.
     *
     * @param count_to_add The number of threads to add to the thread pool
     * @throws std::runtime_error if the thread pool is in an invalid state.
     *
     */

    void thread_pool::add_thread(std::size_t count_to_add)
    {
        // 添加线程
        std::shared_lock<std::shared_mutex> lock_status(pool_status_mutex);
        switch (status.load())
        {
        case status_p::TERMINATED:
        case status_p::TERMINATING:
        case status_p::PAUSED:
            throw std::runtime_error("[thread_pool::add_thread][error]: cannot add threads to the thread pool in this state");
        case status_p::RUNNING:  // 线程池正在运行
        case status_p::SHUTDOWN: // 线程池在等待任务完成，但不再接受新的任务
            break;
        default:
            throw std::runtime_error("[thread_pool::add_thread][error]:unknown status");
        }
        std::unique_lock<std::shared_mutex> lock_worker_list(worker_list_mutex);
        for (std::size_t i = 0; i < count_to_add; i++)
        {
            worker_list.emplace_back(this);
        }
    }

    /**
     * Removes a specified number of threads from thread pool.
     *
     * @param count_to_remove The number of threads to remove from the thread pool.
     * @throws std::runtime_error if the thread pool is an invalid status.
     */

    void thread_pool::remove_thread(std::size_t count_to_remove)
    {
        // 移除min(count_to_remove,work_list.size())个线程
        std::shared_lock<std::shared_mutex> lock_status(pool_status_mutex);
        switch (status.load())
        {
        case status_p::TERMINATED:
        case status_p::TERMINATING:
        case status_p::PAUSED:
            throw std::runtime_error("[thread_pool::remove_thread][error]:cannot remove threads from the thread pool in this status");
        case status_p::RUNNING:
        case status_p::SHUTDOWN:
            break;
        
        default:
            throw std::runtime_error("[thread_pool:remove_thread][error]:unknown status");
        }
        std::unique_lock<std::shared_mutex> lock_worker_list(worker_list_mutex);
        count_to_remove=std::min(count_to_remove,worker_list.size());
        auto it =worker_list.end();
        for(std::size_t i=0;i<count_to_remove;i++)
        {
            --it;
            it->terminate();
        }
        task_queue_cv.notify_all();//唤醒所有等待任务的阻塞线程，以便它们能够检查线程是否被终止
        worker_list.erase(it,worker_list.end());//从线程列表中移除工作线程对象并析构
    }

    std::size_t thread_pool::get_thread_count()
    {
        //获取线程池中线程的数量
        std::shared_lock<std::shared_mutex> lock(worker_list_mutex);
        return worker_list.size();
    }
    
    std::size_t thread_pool::get_task_count()
    {
        std::shared_lock<std::shared_mutex> lock(task_queue_mutex);
        return task_queue.size();
    }

} //namespace thread_utils