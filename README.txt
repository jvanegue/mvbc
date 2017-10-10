Minimal Viable Blockchain by Julien Vanegue (jfv47@cornell.edu)

THIS CODE IS PROVIDED WITH NO WARRANTIES

git clone http://github.com/jvanegue/mvbc

Implementation in C++ requires pthread, openssl and openssl devel packages installed.

To build, just type make in top level directory.

To start node in bootstrap mode:

./node -bootstrap

One your bootstrap node is running, run a worker node. For example, to run
a worker node listening on two ports, you would do:

./node -numworkers 2 -ports 11111 22222

The Bootstrap node should be started before any worker node. Any number of
worker node may be started.

Assuming bootstrap and worker nodes are running on localhost.

Default number of transactions per block is 50000.

See node.h for details of distributed protocol, data structures and API.



