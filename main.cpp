#include <iostream>
#include <thread>
#include<thread>
// 一个简单的任务：计算两个整数的和
int add(int a, int b) {
    // 模拟耗时操作，停止三秒钟
    std::this_thread::sleep_for(std::chrono::seconds(3));
    std::cout<<a+b<<std::endl;
    
}

int main() {
    
    // 执行 add 函数（模拟在新线程中执行）
    // int result = add(10, 20);
    std::thread t(add,20,100);
    // 输出结果
    std::cout << "main thread" << std::endl;
    t.join();
    return 0;
}
