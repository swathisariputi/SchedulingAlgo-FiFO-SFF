#include "io_helper.h"
#include "request.h"

#define MAXBUF (8192)


//
//	TODO: add code to create and manage the buffer


//included header files required
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

//declared and initialized mutex_lock and condition variables
pthread_mutex_t lock_mutex = PTHREAD_MUTEX_INITIALIZER;   //mutex lock variable
pthread_cond_t producer_cv = PTHREAD_COND_INITIALIZER;  // Condition variable for producer(main thread)
pthread_cond_t consumer_cv = PTHREAD_COND_INITIALIZER;  // Condition variable for consumers(worker threads)

// Structure for each request to be stored in buffer queue
typedef struct request
{
    int fd;		                 //fd of request
    char f_name[100];		  //filename of the request
    int f_size;		          //filesize of the request
}req;

//defining queue for requests

req **r=NULL;  // buffer queue for requests.
int count;  //count of number of requests currently present in queue
//rear and front variables to know where request is inserted and removed 
int rear;  
int front;   
int flag=0;   //global variable to check whether buffer request is initialized and memory is allocated 



//initialize buffer queue , allocate memory to it and global varibles
void initialize_queue()
{
	count=0;     //initialized global variables to 0
	front=rear=0;       
	r=(req **)malloc(buffer_max_size*sizeof(req *));
	int i=0;
	while(i<buffer_max_size){
		r[i]=(req*)malloc(sizeof(req));  //allocated memory to each request
		i++;
	}
	flag=1;          //After allocated memory to buffer , flag changes to 1     
}

//For FIFO , I considered buffer queue as circular queue.

//insert request into buffer queue for FIFO scheduling algorithm
void insert_FIFO(int fd,char *file_name,int file_size)          //push request to queue
{
	r[rear]->fd = fd;
	strcpy(r[rear]->f_name,file_name);
	r[rear]->f_size = file_size;
	rear = (rear+1)%buffer_max_size;
	count++;         //count incremented as request inserted
}

//remove request from buffer queue for FIFO scheduling algorithm
req remove_FIFO()                                                 //pop reuest from queue
{
	req request = *r[front];
	front = (front+1)%buffer_max_size;
	count--;   //count decremented as request removed
	return request; //returning removed request (popped request)
}

//For SFF , I considered buffer queue as array based MIN-Heap for SFF scheduling algorithm 

//insert request into buffer queue(array) for SFF scheduling algorithm
void insert_SFF(int fd,char *file_name,int file_size)
{
	r[count]->fd = fd;
	strcpy(r[count]->f_name,file_name);
	r[count]->f_size = file_size;
	count++;         //count incremented as request inserted
}

// converting to heap after inserting new request in SFF
void heap_aftinsert_SFF(int i)
{
	if(i==0){
		return ;
	}
	int parent = (i - 1) / 2; //find parent node
	//For Min-Heap, if current node is smaller than its parent, swap them and call heap_SFF again for the parent
	if(r[parent]->f_size > r[i]->f_size){
		req temp = *r[i];
		*r[i] = *r[parent];
		*r[parent] = temp;
		heap_aftinsert_SFF(parent);    //recursively heapify the parent node
	}	
}

//remove request from buffer queue(array) for SFF scheduling algorithm
req remove_SFF()                                                 
{
	req request = *r[0];
	*r[0] = *r[count-1];
	count--;   //count decremented as request removed
	return request; //returning removed request (popped request)
}

void heap_aftremove_SFF(int i)
{
	int n = count;
	int small = i; // initialize smallest as root
	int left = 2*i+1;
	int right = 2*i+2;	
	//if right child smaller than root
	if(right < n && r[right]->f_size < r[small]->f_size){
		small=right;
	}
	//if left child smaller than root
	if(left < n && r[left]->f_size < r[small]->f_size){
		small=left;
	}
	//if small is not root
	if(small!=i){
		req temp = *r[i];
		*r[i] = *r[small];
		*r[small]=temp;
		heap_aftremove_SFF(small);    //recursively heapify the parent node
	}	
}


//
// Sends out HTTP response in case of errors
//
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];
    
    // Create the body of error message first (have to know its length for header)
    sprintf(body, ""
	    "<!doctype html>\r\n"
	    "<head>\r\n"
	    "  <title>OSTEP WebServer Error</title>\r\n"
	    "</head>\r\n"
	    "<body>\r\n"
	    "  <h2>%s: %s</h2>\r\n" 
	    "  <p>%s: %s</p>\r\n"
	    "</body>\r\n"
	    "</html>\r\n", errnum, shortmsg, longmsg, cause);
    
    // Write out the header information for this response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Type: text/html\r\n");
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
    write_or_die(fd, buf, strlen(buf));
    
    // Write out the body last
    write_or_die(fd, body, strlen(body));
    
    // close the socket connection
    close_or_die(fd);
}

//
// Reads and discards everything up to an empty text line
//
void request_read_headers(int fd) {
    char buf[MAXBUF];
    
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n")) {
		readline_or_die(fd, buf, MAXBUF);
    }
    return;
}

//
// Return 1 if static, 0 if dynamic content (executable file)
// Calculates filename (and cgiargs, for dynamic) from uri
//
int request_parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;
    
    if (!strstr(uri, "cgi")) { 
	// static
	strcpy(cgiargs, "");
	sprintf(filename, ".%s", uri);
	if (uri[strlen(uri)-1] == '/') {
	    strcat(filename, "index.html");
	}
	return 1;
    } else { 
	// dynamic
	ptr = index(uri, '?');
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	} else {
	    strcpy(cgiargs, "");
	}
	sprintf(filename, ".%s", uri);
	return 0;
    }
}

//
// Fills in the filetype given the filename
//
void request_get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) 
		strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) 
		strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) 
		strcpy(filetype, "image/jpeg");
    else 
		strcpy(filetype, "text/plain");
}

//
// Handles requests for static content
//
void request_serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];
    
    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);
    
    // Rather than call read() to read the file into memory, 
    // which would require that we allocate a buffer, we memory-map the file
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);
    
    // put together response
    sprintf(buf, ""
	    "HTTP/1.0 200 OK\r\n"
	    "Server: OSTEP WebServer\r\n"
	    "Content-Length: %d\r\n"
	    "Content-Type: %s\r\n\r\n", 
	    filesize, filetype);
       
    write_or_die(fd, buf, strlen(buf));
    
    //  Writes out to the client socket the memory-mapped file 
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}

//
// Fetches the requests from the buffer and handles them (thread logic)
//
void* thread_request_serve_static(void* arg)
{
	// TODO: write code to actualy respond to HTTP requests
	if(flag==0){         //check whether buffer queue has been allocated memory
		initialize_queue();        //initialize queue if it is not initialized
	}
	while(1){
		req request;
		pthread_mutex_lock(&lock_mutex);  //lock the mutex to prevent data race condition on shared buffer variable

		while(count==0){               // if request queue is empty then put consumer thread to wait
			pthread_cond_wait(&consumer_cv,&lock_mutex);  
		}
		if(scheduling_algo==0){ //FIFO
			request = remove_FIFO();
		}
		else{ //SFF
			request = remove_SFF();
			heap_aftremove_SFF(0);	
		}
		printf("Request for %s is removed from the buffer.\n",request.f_name);
		//printf("\nBuffer size : %d\n",count);
		pthread_cond_signal(&producer_cv);  //signals producer thread to wake up
		pthread_mutex_unlock(&lock_mutex);  //unlocking mutex after request is removed from buffer
		request_serve_static(request.fd,request.f_name,request.f_size);
		close_or_die(request.fd);  //close the connection to client after request has been served
    }
}

//
// Initial handling of the request
//
void request_handle(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];
    
	// get the request type, file path and HTTP version
    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);

	// verify if the request type is GET or not
    if (strcasecmp(method, "GET")) {
		request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
		return;
    }
    request_read_headers(fd);
    
	// check requested content type (static/dynamic)
    is_static = request_parse_uri(uri, filename, cgiargs);
    
	// get some data regarding the requested file, also check if requested file is present on server
    if (stat(filename, &sbuf) < 0) {
		request_error(fd, filename, "404", "Not found", "server could not find this file");
		return;
    }
    
	// verify if requested content is static
    if (is_static) {
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
			request_error(fd, filename, "403", "Forbidden", "server could not read this file");
			return;
		}

		// TODO: write code to add HTTP requests in the buffer based on the scheduling policy
		
		//give error when traversing up the file system
		if(strstr(uri,"..")){
			request_error(fd, filename, "403", "Forbidden", "Traversing up in filesystem is not allowed");
			return ;
		}
		pthread_mutex_lock(&lock_mutex);  //lock the mutex to prevent data race condition on shared buffer variable
		if(flag==0){ //check whether buffer queue has been allocated memory
			initialize_queue();       //initialize queue if it is not initialized
		}	
		while(count==buffer_max_size){                          //Producer inserts request in buffer only if buffer is not full.
			pthread_cond_wait(&producer_cv, &lock_mutex);      // if request queue is maximum then put producer thread to wait
		}
		if(scheduling_algo==0){  //FIFO
		    insert_FIFO(fd,filename,sbuf.st_size);
		} 
		else{  //SFF
			insert_SFF(fd,filename,sbuf.st_size);
			heap_aftinsert_SFF(count-1); //to maintain min-heap
		}
		printf("Request for %s is added to the buffer.\n",filename);
		//printf("\nBuffer size : %d\n",count);
		pthread_cond_signal(&consumer_cv);  //signals consumer thread to wake up
		pthread_mutex_unlock(&lock_mutex);  //unlocking mutex after request is inserted into buffer
    } else {
		request_error(fd, filename, "501", "Not Implemented", "server does not serve dynamic content request");
    }
}
