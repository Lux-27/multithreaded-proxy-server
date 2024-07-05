# Multi-Threaded Proxy Server with Cache

This project implements a multi-threaded proxy server in C, with LRU caching. The parsing of HTTP requests is inspired by existing proxy server implementations.

## Index
- [Project Theory](#project-theory)
- [How to Run](#how-to-run)

## Project Theory
### Introduction
The proxy server acts as an intermediary between a client and a server, handling client requests and forwarding them to the appropriate server. It can enhance performance, security, and monitoring capabilities.

### Basic Working Flow of the Proxy Server:
1. The proxy server receives a client request.
2. It checks if the requested resource is in the cache.
3. If the resource is in the cache, it serves it directly.
4. If not, it forwards the request to the target server, caches the response, and then serves it to the client.

### What does a Proxy Server do?
- Speeds up processes and reduces server-side traffic.
- Restricts users from accessing specific websites.
- Changes the client's IP address to anonymize requests.
- Can encrypt requests to prevent unauthorized access.

### How did we implement Multi-threading?
To handle multiple client requests concurrently, we implemented multi-threading using several synchronization mechanisms, including Semaphores and Mutexes. Here’s a detailed description of each component:
1. Semaphores - Semaphores are used to manage concurrency by controlling access to shared resources. We utilized several semaphore functions in our implementation:
    
    - sem_wait(): This function decrements (locks) the semaphore pointed to by sem. If the semaphore's value is greater than zero, the decrement proceeds, and the function returns immediately. If the semaphore's value is zero, the call blocks until it becomes possible to perform the decrement.
      - Usage: Keeps processes waiting until a semaphore is available, ensuring that only a limited number of threads can access a critical section concurrently.
    
    - sem_post(): This function increments (unlocks) the semaphore pointed to by sem. If the semaphore's value was zero and there are threads waiting for the semaphore to become available, one of the waiting threads is woken up.
      - Usage: Adds the semaphore back, allowing another process to take over the critical section, thereby ensuring orderly access to shared resources.
    
    - sem_getvalue(): This function stores the current value of the semaphore pointed to by sem in the location pointed to by sval.
      - Usage: Retrieves the current value of the semaphore, which can be useful for debugging or monitoring the state of the semaphore.

2. Mutexes - Mutexes (short for "mutual exclusions") are used to prevent race conditions by ensuring that only one thread can access a critical section at a time. We utilized the following mutex functions:

    - pthread_mutex_lock(): This function locks the mutex. If the mutex is already locked by another thread, the calling thread blocks until the mutex becomes available.
      - Usage: Used to enter a critical section, ensuring that only one thread can execute the protected code at a time.
    
    - pthread_mutex_unlock(): This function unlocks the mutex. If there are threads waiting for the mutex to become available, one of the waiting threads is woken up and acquires the mutex.
      - Usage: Used to exit a critical section, allowing other threads to enter the protected code.

### Motivation/Need of Project
To Understand:
- The process of how requests from our local computer reach a server.
- How to manage and handle multiple client requests simultaneously.
- The mechanisms for ensuring safe concurrent access through locking procedures.
- The concept of caching and how various caching functions are utilized by web browsers.

### OS Components Used
- Threading
- Locks
- Semaphore
- Cache (using LRU algorithm)

## How to Run

1. Clone the repository:
   ```sh
   $ git clone https://github.com/Lux-27/multithreaded-proxy-server.git
   ```
   
2. Install Cygwin - if you are using windows OR skip to step 4.
3. Install the following dependencies from Cygwin Installer -
     - all pre-selected dependencies
     - gcc-core (Devel) for C compilation
     - make (devel) for make command
      
4. Go to project directory, using the Cygwin terminal.
    ```sh
    cd [YOUR PROJECT DIRECTORY]
    ```

5. Run make to compiler all files.
    ```sh
    make
    ```

6. Run proxy server at given port number using (if no port no. is given, port 8080 will be used):
    ```sh
    ./proxy <port no.>
    ```
7. Open an Incognito window (or disable browser caching) to test it - 
  - Open http://localhost:port/https://www.cs.princeton.edu/
