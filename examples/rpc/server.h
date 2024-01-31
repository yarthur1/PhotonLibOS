/*
Copyright 2022 The Photon Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once

#include <photon/net/socket.h>
#include <photon/rpc/rpc.h>

#include "protocol.h"

// Generally, RPC server contains with socket server, which provides
// data stream to deleiver RPC data;
// and a RPC skeleton, deal with RPC package and call registered handlers
struct ExampleServer {
    std::unique_ptr<photon::rpc::Skeleton> skeleton;   // SkeletonImpl 负责rpc请求处理
    std::unique_ptr<photon::net::ISocketServer> server;  // 负责连接建立

    ExampleServer()
        : skeleton(photon::rpc::new_skeleton()),  // 创建协程池
          server(photon::net::new_tcp_socket_server()) {   // KernelSocketServer
        skeleton->register_service<Testrun, Heartbeat, Echo, ReadBuffer,
                                   WriteBuffer>(this);  // 注册操作
    }

    // public methods named `do_rpc_service` takes rpc requests
    // and produce response
    // able to set or operate connection directly(like close or set flags)
    // iov is temporary buffer created by skeleton with defined allocator
    // able to use as temporary buffer
    // return value will be droped

    int do_rpc_service(Testrun::Request* req, Testrun::Response* resp,  // 定义操作如何处理 Skeleton::rpc_service会调用
                       IOVector* iov, IStream* conn);

    int do_rpc_service(Echo::Request* req, Echo::Response* resp, IOVector*,
                       IStream*);

    int do_rpc_service(Heartbeat::Request* req, Heartbeat::Response* resp,
                       IOVector*, IStream*);

    int do_rpc_service(ReadBuffer::Request* req, ReadBuffer::Response* resp,
                       IOVector* iov, IStream*);

    int do_rpc_service(WriteBuffer::Request* req, WriteBuffer::Response* resp,
                       IOVector* iov, IStream*);

    // Serve provides handler for socket server
    int serve(photon::net::ISocketStream* stream) {  // stream代表accept 返回的连接，后续请求处理由本函数决定
        return skeleton->serve(stream, false);  // 每个连接都会调用serve
    }

    void term() {
        server.reset();
        skeleton.reset();
    }

    int run(int port);
};