//
// main.cpp
// cpp_docker
//
// 

#include "docker.hpp"
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "...start container" << std::endl;
    docker::container_config config;
    config.host_name = "kaixindeken";
    config.root_dir  = "./kaixindeken";
    config.ip        = "192.168.0.100";
    config.bridge_name = "docker0";
    config.bridge_ip   = "192.168.0.1";
    docker::container container(config);
    container.start();
    std::cout << "stop container..." << std::endl;
    return 0;
}