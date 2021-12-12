//
// docker.hpp
// cpp_docker
//
// 

#include <sys/wait.h>   // waitpid
#include <sys/mount.h>  // mount
#include <fcntl.h>      // open
#include <unistd.h>     // execv, sethostname, chroot, fchdir
#include <sched.h>      // clone

#include <net/if.h>     // if_nametoindex
#include <arpa/inet.h>  // inet_pton

#include <string.h>	    // strcpy
#include <string>	    // std::string

#include "network.h"

#define STACK_SIZE (1024 * 1024)

namespace docker {

    typedef int proc_statu;
    proc_statu proc_err  = -1;
    proc_statu proc_exit = 0;
    proc_statu proc_wait = 1;

    // docker 容器启动配置
    typedef struct container_config {
        std::string host_name;      // 主机名
        std::string root_dir;       // 容器根目录
        std::string ip;             // 容器 IP
        std::string bridge_name;    // 网桥名
        std::string bridge_ip;      // 网桥 IP
    } container_config;

    class container {
    private:
        typedef int process_pid;
        typedef int (*process_func)(void *);

        // 子进程栈
        char child_stack[STACK_SIZE];
        
        // 容器配置
        container_config config;

        // 保存容器网络设备
        char *veth1;
        char *veth2;

        // 设置容器主机名
        void set_hostname() {
            sethostname(this->config.host_name.c_str(), this->config.host_name.length());
        }

        // 设置根目录
        void set_rootdir() {

            // chdir 系统调用, 切换到某个目录下
            chdir(this->config.root_dir.c_str());

            // chrrot 系统调用, 设置根目录, 因为刚才已经切换过当前目录
            // 故直接使用当前目录作为根目录即可
            chroot(".");
        }

        // 设置独立的进程空间
        void set_procsys() {
            // 挂载 proc 文件系统
            mount("none", "/proc", "proc", 0, nullptr);
            mount("none", "/sys", "sysfs", 0, nullptr);
        }

        // 设置网络
        void set_network() {
            int ifindex = if_nametoindex("eth0");
            struct in_addr ipv4;
            struct in_addr bcast;
            struct in_addr gateway;

            int prefix = 16;
            inet_pton(AF_INET, this->config.ip.c_str(), &ipv4);
            inet_pton(AF_INET, "255.255.255.0", &bcast);
            inet_pton(AF_INET, this->config.bridge_ip.c_str(), &gateway);

            // 设置 eth0 IP 地址
            lxc_ipv4_addr_add(ifindex, &ipv4, &bcast, prefix);

            // 启用 lo 网络设备
            lxc_netdev_up("lo");

            // 启用 eth0 网络设备
            lxc_netdev_up("eth0");

            // 设置网关
            lxc_ipv4_gateway_add(ifindex, &gateway);

            // 设置 MAC 地址
            char mac[18];
            new_hwaddr(mac);
            setup_hw_addr(mac, "eth0");
        }

        void start_bash() {
            // 将 C++ std::string 安全的转换为 C 风格的字符串 char *
            // 从 C++14 开始, C++编译器将禁止这种写法 `char *str = "test";`
            std::string bash = "/bin/bash";
            char *c_bash = new char[bash.length()+1];   // +1 用于存放 '\0'
            strcpy(c_bash, bash.c_str());
            
            char* const child_args[] = { c_bash, NULL };
            execv(child_args[0], child_args);           // 在子进程中执行 /bin/bash
            delete []c_bash;
        }

    public:
        container(container_config &config) {
            this->config = config;
        }
        void start() {
            char veth1buf[IFNAMSIZ] = "kaixindeken0X";
            char veth2buf[IFNAMSIZ] = "kaixindeken0X";
            // 创建一对网络设备, 一个用于加载到宿主机, 另一个用于转移到子进程容器中
            veth1 = lxc_mkifname(veth1buf); // lxc_mkifname 这个 API 在网络设备名字后面至少需要添加一个 "X" 
            veth2 = lxc_mkifname(veth2buf); // 用于保证网络设备的正确创建, 详见 network.c 中对 lxc_mkifname 的实现
            lxc_veth_create(veth1, veth2);

            // 设置 veth1 的 MAC 地址
            setup_private_host_hw_addr(veth1);

            // 将 veth1 添加到网桥中
            lxc_bridge_attach(config.bridge_name.c_str(), veth1);

            // 激活 veth1
            lxc_netdev_up(veth1);

            // 容器创建前的一些配置工作
            auto setup = [](void *args) -> int {
                auto _this = reinterpret_cast<container *>(args);
                _this->set_hostname();
                _this->set_rootdir();
                _this->set_procsys();
                _this->set_network();
                _this->start_bash();
                return proc_wait;
            };

            // 使用 clone 创建容器
            process_pid child_pid = clone(setup, child_stack, 
                              CLONE_NEWNS|  // Mount namespace
                              CLONE_NEWNET| // Net   namespace
                              CLONE_NEWUTS| // UTS   namespace
                              CLONE_NEWPID| // PID   namespace
                              SIGCHLD,      // 子进程退出时会发出信号给父进程
                              this);

            // 将 veth2 转移到容器内部, 并命名为 eth0
            lxc_netdev_move_by_name(veth2, child_pid, "eth0");

            waitpid(child_pid, nullptr, 0); // 等待子进程的退出
        }
        ~container() {
            lxc_netdev_delete_by_name(veth1);
            lxc_netdev_delete_by_name(veth2);
        }

    };
}
