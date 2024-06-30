#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// these libraries require either a linux install or a Cygwin based gcc compiler
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>

#include <pthread.h>
#include <semaphore.h>
#include <time.h>

// a window based alternative to the linux based libraries -- TODO
// #include <winsock2.h>
// #include <ws2tcpip.h>
// #include <ws2spi.h>
// #define _WIN32_WINNT 0x601

#define MAX_CLIENTS 10
#define MAX_BYTES 4096
#define MAX_ELEMENT_SIZE 10 * (1 << 20)
#define MAX_SIZE 200 * (1 << 20)

typedef struct cache_element cache_element;

// we are using a time based cache to implement the LRU Cache
// LRU cache is made using a linked list
struct cache_element
{
    char *data;
    int size;
    char *url;
    time_t lru_time_track;
    cache_element *next;
};

cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);
void remove_cache_element();

int port_number = 8080;
int proxy_socketId;

// we are multi-threading the proxy server to handle multiple clients
// each client is handled by a separate thread, hence we need to keep track of the thread id
pthread_t threadId[MAX_CLIENTS];

// the LRU Cache is a shared resource among all the threads there is a possibility of a race condition, hence we need to use a lock
/*
Working : tId1 comes and checks for lock on LRU -> if present it waits for the lock release
else if lock is not present it acquires the lock and performs the operation
*/
// semaphore is used to keep track of the number of threads accessing the cache
sem_t semaphore;
pthread_mutex_t lock;

cache_element *head;
int cache_size;

int connectRemoteServer(char *host_addr, int port_num)
{
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (remoteSocket < 0)
    {
        printf("Failed to create socket for remote server");
        return -1;
    }

    struct hostent *host = gethostbyname(host_addr);
    if (host == NULL)
    {
        fprintf(stderr, "Failed to get host by name - no such host exist");
        return -1;
    }

    struct sockaddr_in server_address;
    bzero((char *)&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port_num);

    bcopy((char *)host->h_addr, (char *)&server_address.sin_addr.s_addr, host->h_length);

    // Connect to Remote server ----------------------------------------------------

    if (connect(remoteSocket, (struct sockaddr *)&server_address, (socklen_t)sizeof(server_address)) < 0)
    {
        fprintf(stderr, "Failed to connect to remote server\n");
        return -1;
    }

    // free(host_addr);
    return remoteSocket;
}

int handle_request(int clientSocketId, struct ParsedRequest *request, char *tempReq)
{
    char *buffer = (char *)malloc(sizeof(char) * MAX_BYTES);
    strcpy(buffer, "GET ");
    strcat(buffer, request->path);
    strcat(buffer, " ");
    strcat(buffer, request->version);
    strcat(buffer, "\r\n");

    size_t len = strlen(buffer);

    if (ParsedHeader_set(request, "Connection", "close") < 0)
    {
        printf("set header key not work\n");
    }

    // check if the request has a host
    if (ParsedHeader_get(request, "Host") == NULL)
    {
        if (ParsedHeader_set(request, "Host", request->host) < 0)
        {
            printf("Set \"Host\" header key not working\n");
        }
    }

    if (ParsedRequest_unparse_headers(request, buffer + len, (size_t)MAX_BYTES - len))
    {
        printf("unparse failed\n");
        // return -1;  // If this happens Still try to send request without header
    }

    int server_port = 80;
    if (request->port != NULL)
    {
        server_port = atoi(request->port);
    }

    int remoteSocketID = connectRemoteServer(request->host, server_port);

    if (remoteSocketID < 0)
    {
        printf("Failed to connect to remote server\n");
        return -1;
    }

    int bytes_send = send(remoteSocketID, buffer, strlen(buffer), 0);
    bzero(buffer, MAX_BYTES);

    bytes_send = recv(remoteSocketID, buffer, MAX_BYTES - 1, 0);
    char *temp_buffer = (char *)malloc(sizeof(char) * MAX_BYTES);

    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while (bytes_send > 0)
    {
        bytes_send = send(clientSocketId, buffer, bytes_send, 0);

        for (size_t i = 0; i < bytes_send / sizeof(char); i++)
        {
            temp_buffer[temp_buffer_index] = buffer[i];
            temp_buffer_index++;
        }

        temp_buffer_size += MAX_BYTES;
        temp_buffer = (char *)realloc(temp_buffer, temp_buffer_size);

        if (bytes_send < 0)
        {
            perror("Failed to send data to client\n");
            break;
        }
        bzero(buffer, MAX_BYTES);
        bytes_send = recv(remoteSocketID, buffer, MAX_BYTES - 1, 0);
    }

    temp_buffer[temp_buffer_index] = '\0';
    free(buffer);
    add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
    free(temp_buffer);
    close(remoteSocketID);
    return 0;
}

int checkHTTPversion(char *msg)
{
    int version = -1;

    if (strncmp(msg, "HTTP/1.1", 8) == 0)
    {
        version = 1;
    }
    else if (strncmp(msg, "HTTP/1.0", 8) == 0)
    {
        version = 1; // Handling this similar to version 1.1
    }
    else
        version = -1;

    return version;
}

int sendErrorMessage(int socket, int status_code)
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);

    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    switch (status_code)
    {
    case 400:
        snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
        printf("400 Bad Request\n");
        send(socket, str, strlen(str), 0);
        break;

    case 403:
        snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
        printf("403 Forbidden\n");
        send(socket, str, strlen(str), 0);
        break;

    case 404:
        snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
        printf("404 Not Found\n");
        send(socket, str, strlen(str), 0);
        break;

    case 500:
        snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
        // printf("500 Internal Server Error\n");
        send(socket, str, strlen(str), 0);
        break;

    case 501:
        snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
        printf("501 Not Implemented\n");
        send(socket, str, strlen(str), 0);
        break;

    case 505:
        snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
        printf("505 HTTP Version Not Supported\n");
        send(socket, str, strlen(str), 0);
        break;

    default:
        return -1;
    }
    return 1;
}

// a thread function to handle the client request
void *thread_fn(void *socketNew)
{
    // wait for the semaphore to be available
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("Semaphore value: %d\n", p);

    // copy the socket id from the void pointer to a local variable -> not necessary
    int *t = (int *)socketNew;
    int socket = *t; // Socket is socket descriptor of the connected Client
    int bytes_sent_by_client, len;

    // Creating buffer of 4kb for a client
    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));
    bzero(buffer, MAX_BYTES); // clear the buffer

    // receive the request from the client
    bytes_sent_by_client = recv(socket, buffer, MAX_BYTES, 0);

    // check if the request is empty
    while (bytes_sent_by_client > 0)
    {
        // check current buffer length
        len = strlen(buffer);
        // check if the request is complete -- HTTP request ends with \r\n\r\n
        if (strstr(buffer, "\r\n\r\n") == NULL)
        {
            // receive the remaining request from the client
            bytes_sent_by_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }
        else
        {
            break;
        }
    }

    // copy the buffer to a temp variable - tempReq, buffer both store the http request sent by client
    char *tempReq = (char *)malloc(strlen(buffer) * sizeof(char) + 1);

    for (size_t i = 0; i < strlen(buffer); i++)
    {
        tempReq[i] = buffer[i];
    }

    // check if the request is in the cache
    struct cache_element *temp = find(tempReq);

    if (temp != NULL)
    {
        // if the request is in the cache, then send the data from the cache to client
        int size = temp->size / sizeof(char);
        int pos = 0;
        char response[MAX_BYTES];

        while (pos <= size)
        {
            bzero(response, MAX_BYTES);
            for (int i = 0; i < MAX_BYTES; i++)
            {
                response[i] = temp->data[pos];
                pos++;
            }
            send(socket, response, MAX_BYTES, 0);
        }

        printf("Data retrieved from cache\n");
        printf("%s\n\n", response);
    }
    else if (bytes_sent_by_client > 0)
    {
        len = strlen(buffer);

        // if the request is not in the cache, then parse the request using parseRequest library
        struct ParsedRequest *request = ParsedRequest_create();
        // ParsedRequest_parse returns 0 on success and -1 on failure.
        // On success it stores parsed request in the request

        if (ParsedRequest_parse(request, buffer, len) < 0)
        {
            printf("Failed to parse the request\n");
            exit(1);
        }
        else
        {
            bzero(buffer, MAX_BYTES);
            // check if the request is a GET request
            // we are only caching the GET requests for now
            // POST, PUT, DELETE requests are implemented yet
            if (!strcmp(request->method, "GET"))
            {
                // check if the request has a host, path and a valid HTTP version
                if (request->host && request->path && checkHTTPversion(request->version) == 1)
                {
                    bytes_sent_by_client = handle_request(socket, request, tempReq);

                    // if our server is not able to handle the request, then send an error message to the client 500
                    if (bytes_sent_by_client == -1)
                    {
                        sendErrorMessage(socket, 500);
                    }
                }
                // if our server cannot parse the request, then send an error message to the client 500
                else
                {
                    sendErrorMessage(socket, 500);
                }
            }
            // if the request is not a GET request, then send an error message to the client 501
            else
            {
                printf("This code only supports GET requests\n");
            }
        }
        // free the memory allocated for the request
        ParsedRequest_destroy(request);
    }

    // if bytes_sent_by_client is less than 0, then there is an error in receiving the request from the client
    else if (bytes_sent_by_client < 0)
    {
        perror("Error in receiving from client.\n");
    }

    // if bytes_sent_by_client is 0, then the client has disconnected
    else if (bytes_sent_by_client == 0)
    {
        printf("Client disconnected\n");
    }

    // close the socket and free the memory allocated for the buffer
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);

    sem_post(&semaphore);
    sem_getvalue(&semaphore, &p);
    printf("Semaphore post value: %d\n", p);
    free(tempReq);
    return NULL;
}

int main(int argc, char *argv[])
{
    int client_socketId, client_length;
    struct sockaddr_in server_address, client_address;

    // initialize semaphore with minimum value 0 and maximum value MAX_CLIENTS
    sem_init(&semaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);

    // allow the user to specify the port number (default is 8080)
    // if argv is 1, then the user has not specified the port number
    // if argv is 2, then the user has specified the port number
    if (argc == 2)
    {
        // ./proxy 8090 - example for specifying the port number
        port_number = atoi(argv[1]);
    }
    else
    {
        printf("No port number specified, using default port number 8080\n");
    }

    printf("Starting proxy server on port %d\n", port_number);

    // using TCP protocol for socket
    // we have a global socket, proxy_socketId, which is used to listen to the incoming requests and is reused for each client
    // AF_INET is the address family for IPv4
    // SOCK_STREAM is the type of socket, which is a stream socket
    // 0 is the protocol, which is IP
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_socketId < 0)
    {
        perror("Failed to create socket");
        exit(1);
    }
    int reuse = 1;
    // set the socket options to reuse the address
    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0)
    {
        perror("Failed to set socket options");
        exit(1);
    }

    // flush the garbage value of out server_address and set the required values
    bzero((char *)&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;          // IPv4
    server_address.sin_port = htons(port_number); // port number
    server_address.sin_addr.s_addr = INADDR_ANY;  // any incoming address
    // bind the socket to the server address
    if (bind(proxy_socketId, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        perror("Port is not available - Bind Failed");
        exit(1);
    }
    printf("Binding on port %d\n", port_number);

    // listen to the incoming requests
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if (listen_status < 0)
    {
        perror("Failed to listen");
        exit(1);
    }

    int i = 0;
    int Connected_socketId[MAX_CLIENTS];
    while (1)
    {
        // accept the incoming request
        bzero((char *)&client_address, sizeof(client_address));
        client_length = sizeof(client_address);
        // accept the incoming request and create a new socket for the client
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_address, (socklen_t *)&client_length);
        if (client_socketId < 0)
        {
            perror("Failed to accept the incoming request");
            exit(1);
        }
        else
        {
            Connected_socketId[i] = client_socketId;
        }

        // copy the client_socketId to a local variable
        struct sockaddr_in *client_ptr = (struct sockaddr_in *)&client_address;
        struct in_addr ipAddr = client_ptr->sin_addr;
        // extract the IP address from the client_address
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ipAddr, str, INET_ADDRSTRLEN); // format the IP address
        printf("Client is connected with port %d and IP address %s\n", ntohs(client_ptr->sin_port), str);

        // if client is not new, then we can reuse the socket from the previous instance of client
        // if client is new, then we need to create a new socket for the client -> Created above
        pthread_create(&threadId[i], NULL, thread_fn, (void *)&Connected_socketId[i]);
        i++;
    }
    close(proxy_socketId); // return memory to the system
    return 0;
}

cache_element *find(char *url)
{
    cache_element *site = NULL;
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Removing cache element, Lock Acquired %d\n", temp_lock_val);

    if (head != NULL)
    {
        site = head;
        while (site != NULL)
        {
            if (!strcmp(site->url, url))
            {
                printf("LRU time track before: %ld\n", site->lru_time_track);
                printf("URL found in cache\n");
                site->lru_time_track = time(NULL);
                printf("LRU time track after: %ld\n", site->lru_time_track);
                break;
            }
            site = site->next;
        }
    }
    else
    {
        printf("url not found\n");
    }

    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Removing cache element, Lock Released\n");

    return site;
}

int add_cache_element(char *data, int size, char *url)
{
    // Adds element to the cache
    // sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Add Cache Lock Acquired %d\n", temp_lock_val);
    int element_size = size + 1 + strlen(url) + sizeof(cache_element); // Size of the new element which will be added to the cache
    if (element_size > MAX_ELEMENT_SIZE)
    {
        // sem_post(&cache_lock);
        //  If element size is greater than MAX_ELEMENT_SIZE we don't add the element to the cache
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
        // free(data);
        // printf("--\n");
        // free(url);
        return 0;
    }
    else
    {
        while (cache_size + element_size > MAX_SIZE)
        {
            // We keep removing elements from cache until we get enough space to add the element
            remove_cache_element();
        }
        cache_element *element = (cache_element *)malloc(sizeof(cache_element)); // Allocating memory for the new cache element
        element->data = (char *)malloc(size + 1);                                // Allocating memory for the response to be stored in the cache element
        strcpy(element->data, data);
        element->url = (char *)malloc(1 + (strlen(url) * sizeof(char))); // Allocating memory for the request to be stored in the cache element (as a key)
        strcpy(element->url, url);
        element->lru_time_track = time(NULL); // Updating the time_track
        element->next = head;
        element->size = size;
        head = element;
        cache_size += element_size;
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
        // sem_post(&cache_lock);
        //  free(data);
        //  printf("--\n");
        //  free(url);
        return 1;
    }
    return 0;
}

void remove_cache_element()
{
    // If cache is not empty searches for the node which has the least lru_time_track and deletes it
    cache_element *p;    // Cache_element Pointer (Prev. Pointer)
    cache_element *q;    // Cache_element Pointer (Next Pointer)
    cache_element *temp; // Cache element to remove
    // sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove Cache Lock Acquired %d\n", temp_lock_val);
    if (head != NULL)
    { // Cache != empty
        for (q = head, p = head, temp = head; q->next != NULL; q = q->next)
        { // Iterate through entire cache and search for oldest time track
            if (((q->next)->lru_time_track) < (temp->lru_time_track))
            {
                temp = q->next;
                p = q;
            }
        }
        if (temp == head)
        {
            head = head->next; /*Handle the base case*/
        }
        else
        {
            p->next = temp->next;
        }
        // if Cache is not empty, search for the node which has the least lru_time_track and delete it

        cache_size = cache_size - (temp->size) - sizeof(cache_element) - strlen(temp->url) - 1; // updating the cache size
        free(temp->data);
        free(temp->url); // Free the removed element
        free(temp);
    }
    // sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Remove Cache Lock Released %d\n", temp_lock_val);
    return;
}