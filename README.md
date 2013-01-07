Bandwidth Estimator
================

Bandwidth Estimator is yet another Linux-tool for estimating available bandwidth and was developed out of frustration with iperf. Among others, iperf requires both generator and receiver to have public IPs, and the application misbehaves when packets are lost. Sessions are not properly ended and bandwidth estimations are made over too long intervals.

Bandwidth estimator consists of two application, a receiver and a traffic generator, and only the generator is required to have a public IP (or an IP accessible from the receiver). The receiver instructs the traffic generator by providing desired bandwidth and so forth. It supports both UDP and TCP, and IPv4 and v6, but is currently limited to downlink measurements. Uplink is at the top of the todo-list. Also, by providing the receiver with a file name, the arrival time of each packet is written to a file. This allows you to easily detect and plot for example buffering in the network. The latter is a huge problem in Mobile Broadband Network, which is where this tool has been used. For UDP, the timestamp is provided by the kernel, while TCP uses userspace timestamps due to its stream-nature.

In order to compile the application using the provided Makefile, CMake is required. Otherwise, there are no external dependencies. The application currently only runs on Linux, but except for a few system calls, it only uses platform-independent functionality. I dont have a Windows-machine, but if anyone is interested in porting it, feel free and please let me know :)

To see the available command line options, start either application without any arguments. Also, the generator creates a thread pool and serves one receiver per thread. If you want to change the number of threads, change the value of NUM\_THREADS at the top of bw\_estimation\_generator.c
