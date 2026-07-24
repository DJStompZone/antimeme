cmake_minimum_required(VERSION 3.10)
project(DockerCredentialAntimeme)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(docker-credential-antimeme antimeme.cpp)

target_link_libraries(docker-credential-antimeme Crypt32.lib)
