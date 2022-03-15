# 使用 Namespace 进行资源隔离



## 创建容器子线程

在项目目录下创建 cpp-docker 文件夹，并在文件夹下新建 docker.hpp 文件，在其中我们首先创建一个名叫 docker 的命名空间，以供我们的外部代码进行调用。

```c++
#include <sys/wait.h>   // waitpid
#include <sys/mount.h>  // mount
#include <fcntl.h>      // open
#include <unistd.h>     // execv, sethostname, chroot, fchdir
#include <sched.h>      // clone

// C 标准库
#include <cstring>

// C++ 标准库
#include <string>       // std::string

#define STACK_SIZE (512 * 512) // 定义子进程空间大小

namespace docker {
    // 具体容器实现代码
}
```

先定义一些增加可读性的变量：

```c++
// 在 namespace docker 内部定义
typedef int proc_statu;
proc_statu proc_err  = -1;
proc_statu proc_exit = 0;
proc_statu proc_wait = 1;
```

在定义容器类之前，我们先分析一下定义一个容器需要哪些参数，暂时不考虑网络相关的配置，那么，从一个镜像创建一个 Docker 容器，只需要指定主机名字、 以及镜像的位置即可，因此：

```c++
// docker 容器启动配置
typedef struct container_config {
    std::string host_name;      // 主机名
    std::string root_dir;       // 容器根目录
} container_config;
```

定义容器类 container，并让它在构造方法中完成对容器的相关配置：

```c++
#include "docker.hpp"
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "...start container" << std::endl;
    docker::container_config config;
    
    // 配置容器
    
    docker::container container(config);// 根据 config 构造容器
    container.start();                  // 启动容器
    std::cout << "stop container..." << std::endl;
    return 0;
}
```

在 main.cpp 中，为了让容器的启动变得简洁易懂，假设让容器通过一个 start() 方法启动。

现在我们回到 docker.hpp 中来，实现 start() 这个方法：

```c++
void start() {
    auto setup = [](void *args) -> int {
    	auto _this = reinterpret_cast(args);
        // 对容器进行相关配置
        // ...

        return proc_wait;
    };

    process_pid child_pid = clone(setup, child_stack + STACK_SIZE, // 移动到栈底
                        SIGCHLD,      // 子进程退出时会发出信号给父进程
                        this);
    waitpid(child_pid, nullptr, 0); // 等待子进程的退出
}
```

docker::container::start() 这个方法使用了 clone 这个 Linux 的系统调用，同时， 为了让回调函数 setup 顺利获取到我们的 docker::container 实例对象，可以通过 clone() 中的第四个参数进行传递，这里，我们传递了 this 指针。

而对于 setup 而言，使用了 lambda 表达式创建 setup 这个回调函数。 在 C++ 中，捕获列表为空的 lambda 表达式能够作为函数指针进行传递。因此，setup 也就成为了传递给 clone() 的回调函数。

在这个 container 这个类的构造函数中，我们定义了一个要被 clone() 系统调用所需要调用的子进程处理函数， 这个函数的返回值被我们使用了 typedef 改写为了 proc_statu，当这个函数返回 proc_wait 时， 就会让 clone 后的子进程等待到结束时再退出。

这还不够，因为我们还没有在进程中进行任何配置，可想而知，当这个进程启动后，由于什么事情都没有做就直接返回了， 我们的程序也就会立即退出。我们知道，在 Docker 中，为了让一个容器保持运行状态，我们可以使用：

```shell
docker run -it ubuntu:14.04 /bin/bash
```

将 STDIN 绑定到容器的 /bin/bash 中，所以我们可以给 docker::container 类编写一个 start_bash()：

```c++
private:
void start_bash() {
    // 将 C++ std::string 安全的转换为 C 风格的字符串 char *
    // 从 C++14 开始, C++编译器将禁止这种写法 char *str = "test";
    std::string bash = "/bin/bash";
    char *c_bash = new char[bash.length()+1];   // +1 用于存放 '\0'
    strcpy(c_bash, bash.c_str());
    
    char* const child_args[] = { c_bash, NULL };
    execv(child_args[0], child_args);           // 在子进程中执行 /bin/bash
    delete []c_bash;
}
```

并在 setup 中调用：

```c++
auto setup = [](void *args) -> int {
    auto _this = reinterpret_cast<container *>(args);
    _this->start_bash();
    return proc_wait;
}
```

```shell
$ hostname
gohb-virtual-machine
$ g++ main.cpp -std=C++11
$ ./a.out
..start container
# hostname
iZ23kcx72c8Z
# ls
a.out docker.hpp main.cpp
# mkdir test
# ls
a.out docker.hpp main.cpp test
# exit
exit
stop container...
$ ls
a.out docker.hpp main.cpp test
```

在上面的操作中，我们首先查看了当前的 hostname，并编译我们目前为止编写的代码，运行，并进入了我们的容器。可以看到，进入容器后，bash 的显示发生了变化，这和我们的目的是一致的。

但是，很容易发现，这并不是我们想要的结果，因为这简直就和我们的宿主机一模一样，在这个『容器』里的操作会直接影响到宿主机。

这时，我们就要在 clone 这个 API 中引入需要的 Namespace 了。



## 使容器具备自己的主机名

通过系统调用设置子进程的 hostname 只需要一句话，于是我们为 docker::container 这个类创建一个私有方法：

```c++
private:
// 设置容器主机名
void set_hostname() {
  sethostname(this->config.host_name.c_str(), this->config.host_name.length());
}
```

并将 start()，更改为：

```c++
void start() {
    auto setup = [](void *args) -> int {
        auto _this = reinterpret_cast<container *>(args);

        // 对容器进行相关配置
        _this->set_hostname();
        _this->start_bash();
        
        return proc_wait;
    };

    process_pid child_pid = clone(setup, child_stack+STACK_SIZE, 
                        CLONE_NEWUTS| // 添加 UTS namespace
                        SIGCHLD,      // 子进程退出时会发出信号给父进程
                        this);
    waitpid(child_pid, nullptr, 0); // 等待子进程的退出
}
```

并在 main.cpp 中配置 hostname 的名字：

```c++
int main(int argc, char** argv) {
    std::cout << "...start container" << std::endl;
    docker::container_config config;
    config.host_name = "kaixindeken";
...
```

这时，我们再重新编译：

```shell
$ g++ main.cpp -std=C++11
$ ./a.out
..start container
stop container...
```

容易发现，我们的容器直接就退出了，这是因为当引入了 Namespace 后，我们的程序就需要超级用户权限的支持了，因此，我们在执行程序前加入 sudo：

```shell
$ hostname
iZ23kcx72c8Z
$ ./a.out
..start container
# hostname
kaixindeken
# exit
exit
stop container...
```

然而这还远远达不到容器的效果，因为我们通过 ls 可以看到，我们依然能访问宿主机的目录。

在 Docker 技术中，容器是基于镜像进行创建的。既然我们要实现容器，自然也不例外的要基于镜像进行创建，可以从：

```shell
wget http://labfile-10066424.cos.myqcloud.com/docker-image.tar
```

然后解压他们到项目文件夹(先用 mkdir 创建好)下，解压完成后，我们就能够看到有一个差不多还算完整的 Linux 目录了：

```shell
$ ls kaixindeken
bin dev home lib64 mnt proc run boot etc lib media opt root sbin
```

现在，我们让 docker::container 在启动的时，就进入到这个目录下，并以这个目录为根目录，屏蔽掉子进程对外的访问：

```
private:
// 设置根目录
void set_rootdir() {

    // chdir 系统调用, 切换到某个目录下
    chdir(this->config.root_dir.c_str());

    // chroot 系统调用, 设置根目录, 因为刚才已经切换过当前目录
    // 故直接使用当前目录作为根目录即可
    chroot(".");
}
```

然后在 main.cpp 中填写好相关配置：

```c++
#include "docker.hpp"
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "...start container" << std::endl;
    docker::container_config config;
    config.host_name = "kaixindeken";
    config.root_dir  = "./kaixindeken";
...
```

并在 clone() 这个调用里启用 CLONE_NEWNS 来开启 Mount Namespace：

```c++
void start() {
    auto setup = [](void *args) -> int {
        auto _this = reinterpret_cast<container *>(args);
        _this->set_hostname();
        _this->set_rootdir();
        _this->start_bash();
        return proc_wait;
    };

    process_pid child_pid = clone(setup, child_stack+STACK_SIZE, 
                      CLONE_NEWUTS| // UTS   namespace
                      CLONE_NEWNS|  // Mount namespace
                      SIGCHLD,      // 子进程退出时会发出信号给父进程
                      this);
    waitpid(child_pid, nullptr, 0); // 等待子进程的退出
}
```

这时，我们再重新编译，透过 ls 便能看到这个子进程已经生活在了一个完整的 linux 目录下了。

```shell
$ sudo./a.out
.start container
$ ls
bin dev home lib64 mnt proc run boot etc lib media opt root sbin
$ hostname
kaixindeken
```



## 使容器具备自己的进程系统

如果我们使用 ps、top 这样的命令，仍然能够观察到父进程中的全部进程。

```shell
$ sudo ./a.out
start container
# ps
Error, do this: mount -t proc proc/proc
# mount -t proc proc/proc
# ps
PID TTY TIME CMD
4854 ? 00:00:00 sudo
4855 ? 00:00:00 a.out
4856 ? 00:00:00 bash
4861 ? 00:00:00 pS
```

为了解决这个问题，我们还需要引入 PID Namespace 来隔离子进程和父进程他们的 PID 空间。

```c++
private:
// 设置独立的进程空间
void set_procsys() {
    // 挂载 proc 文件系统
    mount("none", "/proc", "proc", 0, nullptr);
    mount("none", "/sys", "sysfs", 0, nullptr);
}
```

同样，我们依然需要在 start() 里面增加这部分内容，引入 CLONE_NEWPID ：

```c++
void start() {
    auto setup = [](void *args) -> int {
        auto _this = reinterpret_cast<container *>(args);
        _this->set_hostname();
        _this->set_rootdir();
        _this->set_procsys();
        _this->start_bash();
        return proc_wait;
    };

    process_pid child_pid = clone(setup, child_stack, 
                      CLONE_NEWUTS| // UTS   namespace
                      CLONE_NEWNS|  // Mount namespace
                      CLONE_NEWPID| // PID   namespace
                      SIGCHLD,      // 子进程退出时会发出信号给父进程
                      this);
    waitpid(child_pid, nullptr, 0); // 等待子进程的退出
}
```

这时，我们再重新编译运行，便能够看到：

```c++
$ g++ main. cpp -std=c++11
$ sudo ./a.out
start container
# ps
PID TTY TIME     CMD
1   ?   00:00:00 bash
4   ?   00:00:00 ps
```

这个容器已经具有独立的进程空间了，并且不会出现其他的问题。
