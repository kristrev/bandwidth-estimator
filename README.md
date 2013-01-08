Bandwidth Estimator
================

Bandwidth Estimator is yet another Linux-tool for estimating available
bandwidth. It was developed out of frustration with iperf, as well as a desire
to easier get a look into how packets are received. The latter is especially
important in mobile broadband networks, as they are both statefull and full of
middleboxes that often do nasty things. On the machines I have tested, the
generator is able to send packets at up to 950~Mbit/s.

Bandwidth estimator consists of two application, a receiver and a traffic
generator, and, unlike iperf, only the generator is required to have a public IP
(or an IP accessible from the receiver). The receiver instructs the traffic
generator by providing desired bandwidth, duration of the test and so forth. The
application supports both UDP and TCP, and IPv4 and v6, but is currently limited
to downlink measurements. Uplink is at the top of the todo-list. 

By providing the receiver with a file name, the arrival time of each packet is
written to a file. This can be processed using the included python-script
(plot\_recv\_times.py), which accepts an interval (in ms) and the log file name
as input parameters. It then sums up how many bytes were received for each
interval (including empty intervals).

Both the raw and the parsed file are easy to plot and enables easy spotting of
network artifacts, like buffering. The latter occurs frequently in mobile
broadband networks, for example when a device moves or a channel becomes
congested. Kernel timestamps are used with UDP, while userspace timestamps are
applied to TCP due to its stream nature. If a higher timestamp-resolution is
needed, for example tcpdump can be used.

In order to compile the application using the provided Makefile, CMake is
required. Otherwise, there are no external dependencies.

The receiver accepts the following command line options:

* -b : Bandwidth in Mbit/s (required).
* -t : Duration of tests in seconds (required).
* -l : Payload length in bytes, only required with UDP.
* -s : Source IP to bind to (required).
* -d : IP of traffic generator (required).
* -p : Port at traffic generator (required).
* -r : Use TCP instead of UDP (optional).
* -w : Filename for writing packet timestamps (optional).

The generator accepts the following command line options:

* -s : Source IP to bind to (required).
* -p : Source port (required).

Worth notin is that the generator creates a thread pool and serves one receiver
per thread. If you want to change the number of threads, change the value of
NUM\_THREADS at the top of bw\_estimation\_generator.c
