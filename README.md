# FEUP-SOPE-ThreadedServer
**Description** - Client and Server that operate using a threadpool, aided by mutexes, semaphores, and FIFOs.

**Course** - Operating Systems

## Project Overview

The Threaded Server is a simple server that can handle multiple requests from multiple local clients at the same time, by creating a new thread for each request. The server can receive and send messages to each client, and it also supports file transfer between the server and clients.


## Usage

To run the server & client, simply compile the source codes using the correspondent provided Makefile and then execute the resulting binary. The server will start listening for incoming connections on the specified FIFO file.


