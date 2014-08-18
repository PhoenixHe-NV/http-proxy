
http-proxy
=============

已完成部分
=============
###全部

额外完成内容
=============
0. 同时支持HTTP 1.1与HTTP 1.0
1. 支持Connection: Keep-Alive
2. 支持CONNECT请求，因此支持https请求
3. 从监听到转发请求，全面的ipv6支持
4. 单线程epoll驱动，可支持上千并发请求
5. 可配置的log文件位置
6. 可配置的log level，支持verbose模式
7. 可配置的监听地址，支持监听域名，监听ipv6地址

可处理的URL
==============
0. http://www.bilibili.com
1. http://www.bilibili.com/
2. http://www.bilibili.com/video/av32175/
3. http://www.bilibili.com:80/video/av32175/ (指定端口后，proxy会通过正确的端口发起请求）
4. https://www.google.com.hk:80/?q=http-proxy (浏览器会通过CONNECT发起请求)

Multiple Request的处理方法
==============
使用单线程epoll驱动，基于ucontext上下文切换的coroutine。

###0. 关于epoll
epoll是一系列linux系统调用，简单的说，可以向内核注册一系列监听的fd，然后调用epoll_wait()，内核会返回一系列从不可读写状态变为可读写状态的fd。在这里使用的是Edge Trigger模式，所有的fd都被设置为non blocking。关于epoll具体请参考man手册，epoll相关代码请参考core/net_pull.c。

###1. 关于ucontext
简单来说，ucontext支持在一系列调用栈之间切换。例如，func_a()在运行过程中，malloc了一段内存作为func_b()的调用栈，并且把对于func_b()调用的参数压入新的栈里，再调用swapcontext()即可切换到func_b()中，在func_b()中也可随时切换回func_a()的调用栈。关于ucontext具体请参考man手册，ucontext相关代码请参考core/async.c。

###2. Coroutine
本质上，coroutine是多个运行的func在不同的调用栈上，核心控制代码在这些调用栈上切换。考虑一个req_handler()在某个调用栈上，当handler尝试去读请求的startline的时候发现会block（比如浏览器此时尚未把startline发送过来），这时候req_handler()会告诉上层，自己会在某个fd的读操作上block，然后切换回主的调用栈。回来之后，主程序会把这个fd的read事件注册到epoll里面，等到epoll发现这个fd又变为可读了，再回来切换到req_handler()的调用栈。这些代码请参考core/conn.c。这样做的优点是编程逻辑简单，req_handler()只需要按照正常的处理逻辑去写就行了。缺点是，相对于另一种解决方案“状态机”，coroutine会在调用栈上消耗更多内存，并且在调用栈之间进行上下文切换的效率也不及状态机。但是，状态机的转移写起来比较困难，而且这样的上下文切换已经比多线程的上下文切换要快很多了。

###3. HTTP 1.1与多连接
HTTP 1.1相对于HTTP 1.0而言，主要的不同之处有两个。一个是body length在HTTP 1.1里面可以是Transfer-Encoding: chunked。另一个是TCP连接的保持，即Connection: keep-alive。第一点很好处理，在读取body部分的时候判断一下即可。第二点，则是这里主要想讨论的东西。
浏览器可以以如下方式发起请求：建立单个连接到proxy server，一口气在连接上发出一连串的HTTP请求，这个过程被称作HTTP pipelining。此时，显然一个个的去处理请求（dns lookup、建立连接、向远端服务器发起请求、等待回应、copy回请求、再去poll下一个请求）效率是非常低的，因为每一步的延迟加起来是非常可观的，而这么大的延迟才能处理一个请求。

对于pipeline的请求，我的处理方案是：（代码参考net_handle.c）
```
                                    net_req_handler() -------|
                                        | Fetch and Parse    | Send Request
                                        v                    v
Browser    req_list head_req->some_req->tail_req            Server Connection pool
                    ^                                       |
                    | Copy back to client                   v
                net_rsp_handler() <--------------------------
```             
net_req_handler()和net_rsp_handler()分别在两个不同的调用栈上，因为他们可能block在不同的fd操作上，比如req会block在client上，而rsp会block在read from server或者write to client上面，而我的设计是，一个调用栈一次只能block在一个fd的一个IO操作上(recv或者send）。net_req_handler()做的事情很简单，解析client发起的请求，向远端服务器发起请求，并把请求放到请求队列里面。net_rsp_handler()会从请求队列的头上读取请求，然后从远端服务器把响应按顺序复制回client。简单的架构就是这样的。

在server connection pool中，加入了一点优化。首先，struct conn_endpoint（参考conn.c）保存了ip和端口。Connection Pool会以struct conn_endpoint为hash key对已经建立的连接进行保存，这样，在Browser对同一个ip的同一个端口发起pipeline的请求的时候，proxy自己也可以对远端服务器发起pipeline的请求，只要保证所有响应复制回来的顺序没问题就行了。当然，为了达到这一点，一个dns cache也是必不可少的。



已知的Bug
================
0. 某些网站无法正常登陆，比如qq，原因不明。

1. 在某些情况下proxy会段错误崩溃，原因不明。

2. 有一个比较严重的问题：一个context可能会block在一个dns请求上，但是dns缺乏超时机制，导致了所有超时的dns请求会引起内存泄露，并且block掉了整个请求的TCP连接。如果所有dns请求都会超时，那么这是非常个非常严重的问题，所有资源都得不到释放。然而，给getaddrinfo加上一个超时机制，是一件不太容易的事情。


