# Control Group 限制容器资源

先编写一个占用 CPU 的程序：

```c++
#include <iostream>
#include <unistd.h>

int main() {
    std::cout << "pid: " << getpid() << std::endl;
    long long num = 0;
    while(1) {
        num++;
    }
    return 0;
}
```

这个程序不会停下来，并且会一直占用 CPU。可以使用 top -p 来进行观察。这时候我们运行编译后的结果，可以看到，CPU 资源的使用较刚才明显上升。

现在我们来限制这个容器对我们宿主机 CPU 的使用。

限制 CPU 资源的使用，需要配置两个东西：

- 要限制的进程 ID: 位于 /sys/fs/cgroup/cpu/cpp-docker/tasks
- 限制的 CPU 配额: 位于 /sys/fs/cgroup/cpu/cpp-docker/cpu.cfs_quota_us

```shell
sudo su     # 切换到 root
echo 20000 > /sys/fs/cgroup/cpu/cpp-docker/cpu.cfs_quota_us # 限制为 20% 的使用率
echo 输出的进程PID > /sys/fs/cgroup/cpu/cpp-docker/tasks
```

通过这两个配置之后，我们便能够立即看到 top 里的 CPU 使用发生了骤降。

这也就完成了对 CPU 资源的限制。此外，我们还可以对内存进行限制。