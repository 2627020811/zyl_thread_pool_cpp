具体包括两个大类：线程池类和工作线程类，其中工作线程类是在线程池类中定义的，对两个类的操作和管理进行解耦

## 1. 定义线程池类

- 线程池类内私有部分：工作线程类（主要用于实现对工作线程的管理、线程池状态变量、任务队列、线程池任务列表数量限制、工作线程链表（删加比较方便）、线程池同步相关的锁和条件变量、线程池状态管理的原子操作函数（pause_with_status_lock等）
- 线程池类内公有部分：构造与析构函数，任务提交函数、线程池状态管理的操作函数（pause()等）、线程管理函数



说明：

- 线程池分为五个状态：线程池的状态：-1：线程池已终止，0：线程池将终止，1：线程池正在运行，2：线程池被暂停，状态之间可以用状态管理函数进行转化变迁
- 线程池状态管理的操作函数和线程池状态管理的原子操作函数是一一对应的，程池状态管理的操作函数取得状态锁之后去调用线程池状态管理的原子操作函数，之所以分开写两个函数并且原子操作是私有的是因为在一些状态变迁（比如恢复线程池）等也需要用到状态管理原子操作去进行状态变迁。
-  禁用拷贝构造函数、移动构造函数、赋值运算符：防止线程池被拷贝或者赋值，线程池不应该被被拷贝或者赋值
- 任务提交函数submit是模板函数，内部使用可变参数模板与完美转发等现代C++特性实现



## 2.定义工作线程类

- 工作线程私有部分：状态变量、线程同步的一些锁、暂停时用到的信号变量、线程状态管理的原子操作函数（terminate_with_status_lock等）
- 工作线程公有部分：构造函数与析构函数、线程状态管理函数（terminate等）



说明：

- 线程同样分为五种状态：状态变量类型，-1：线程已终止，0：线程将终止，1：线程正在运行，2：线程被暂停，3：线程在阻塞等待新任务
- std::binary_semaphore pause_sem; 信号量，因为我们的状态里有线程暂停这个选项，具体而言使用信号量实现的暂停