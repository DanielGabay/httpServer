Authored by: Daniel Gabay.
"ex3" http server

==Program Files==
server.c -> main program
threadpool.c -> used by the server
README.txt - instructions

==Description==
<----threadpool.c---->
This file implements the functionality of threadpool.h
In order to use it's the pool,it's need to be initialized by calling create_threadpool() method.
on succsess it returns a pointer to threadpool.
The "pool" is implemented by a queue of jobs. To add new job, call dispatch() method with the needed params.
Each "new job" is added into the queue, and waits there until some thread is available to handel it.
Note: 1)Each work_t ojbect (what's iv'e mantiones as "job") contains an argument & a pointer to function.
         When the thread "handel" the job, it's actualy calls the function with the argument.
       2)In oreder to enalbe a clean working multithreaded program, each time a thread want's to get access
         to the queue/threadpool var's, it thread must get the mutex lock, o.w he need to wait.
		 
<----server.c---->
This program implements an HTTP server.
The server supports only GET method, request protocol can by sent by: HTTP/1.0 & HTTP/1.1,
but the response is always HTTP/1.0.
The server is able to:
      1) read & analyze client's request.
      2) Constructs an HTTP response based on client's request.
      3) Sends the response to the client.

The server should handle the connections with the clients (using TCP) and creates a socket
for each client it talks to. In order to enable multithreaded program,
the server should create threads that handle the connections withthe clients.
Since, the server should maintain a limited number of threads, it constructs a thread pool.
Command line usage: server <port> <pool-size> <max-number-of-request>
The response of the server depends on the the client's request.
There are 3 main response categories:
      1)Error -> internal error or client's request error
      2)File content -> when requesting a file that the client has premission to read, the server will send it back.
      3)Dir content -> an HTML table contains all folder content



==How to compile?==
gcc -o server server.c threadpool.c threadpool.h -lpthread -Wall -g

==Input:==
The server gets 3 parameters: port number, threadpool size, max number of requests at this order.
example how to run: ./server 8888 5 20    ---> means that port is 8888, pool size is 5, max number of requests is 20.
if one or more of the parameters is missing/less or equal then zero, a usage error will be printed and the program will end.

==Output:==
The server is only wait for requests and d'ont print nothing. when there is a request from some client,
the server will handle that request and will send a response back to the client.
