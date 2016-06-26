CSci4061 Project 4

name: Zach Foster

Tested on x86_64 Linux lab machine.

## Compilation
To compile the program use the following command in the SourceCode directory:

$ make clean

$ make

## Execution
After the executable has been created, use the following command to run the program:

$ ./web_server_http \<port> \<web_root> \<num_dispatch> \<num_worker> \<queue_size>

**\<port>**: port number

**\<web_root>**: path to the root directory of the web server files.

**\<num_dispatch>**: number of dispatcher threads

**\<num_worker>**: number of worker threads

**\<queue_size>**: size of the request queue

## Purpose
This program uses supplied functions for all server communication. Descriptions of given functions can be found in util.h.

The main purpose of this program is to use POSIX threads, condition variables, and the supplied server functions to create a multi-threaded web server to handle GET requests. 

## Threads and Logging
The program has two types of threads, dispatcher and worker. The dispatcher threads accept incoming connections, read the request from the connection, and store the requests in the queue. The worker threads wait for requests in the queue, and fulfill the requests back to the client. Worker threads also record summaries of each request to a log file in the following format:

[Thread#][Request#][fd][request][byte/error]

**Thread#**: The worker thread ID

**Request#**: The total amount of request this worker thread has handled thus far

**fd**: The file descriptor 

**request**: The string of the file name buffer

**byte/error**: Either the number of bytes returned or the error message

^C is necessary to exit the application.
