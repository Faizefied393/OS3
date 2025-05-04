#include "io_helper.h"
#include "request.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define MAXBUF (8192)
#define MAX_QUEUE_SIZE 64

//
// Global server parameters
//
int num_threads = DEFAULT_THREADS;
int buffer_max_size = DEFAULT_BUFFER_SIZE;
int scheduling_algo = DEFAULT_SCHED_ALGO;

//
// Request structure
//
typedef struct {
    int fd;
    char filename[MAXBUF];
    int filesize;
} request_t;

//
// Shared request queue and synchronization
//
request_t request_buffer[MAX_QUEUE_SIZE];
int buffer_count = 0;

pthread_mutex_t buffer_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t buffer_not_full = PTHREAD_COND_INITIALIZER;

//
// Enqueue a request into the buffer
//
void enqueue_request(int fd, const char *filename, int filesize) {
    pthread_mutex_lock(&buffer_lock);

    while (buffer_count >= buffer_max_size)
        pthread_cond_wait(&buffer_not_full, &buffer_lock);

    request_buffer[buffer_count].fd = fd;
    strncpy(request_buffer[buffer_count].filename, filename, MAXBUF - 1);
    request_buffer[buffer_count].filename[MAXBUF - 1] = '\0';
    request_buffer[buffer_count].filesize = filesize;
    buffer_count++;

    pthread_cond_signal(&buffer_not_empty);
    pthread_mutex_unlock(&buffer_lock);
}

//
// Remove a request from the buffer based on scheduling
//
request_t dequeue_request() {
    pthread_mutex_lock(&buffer_lock);

    while (buffer_count == 0)
        pthread_cond_wait(&buffer_not_empty, &buffer_lock);

    int index = 0;

    if (scheduling_algo == 1) { // SFF
        for (int i = 1; i < buffer_count; i++) {
            if (request_buffer[i].filesize < request_buffer[index].filesize) {
                index = i;
            }
        }
    } else if (scheduling_algo == 2) { // Random
        index = rand() % buffer_count;
    }

    request_t req = request_buffer[index];

    for (int i = index; i < buffer_count - 1; i++) {
        request_buffer[i] = request_buffer[i + 1];
    }

    buffer_count--;

    pthread_cond_signal(&buffer_not_full);
    pthread_mutex_unlock(&buffer_lock);

    return req;
}

//
// Thread worker that serves requests
//
void* thread_request_worker(void* arg) {
    while (1) {
        request_t req = dequeue_request();
        request_serve_static(req.fd, req.filename, req.filesize);
        close_or_die(req.fd);
    }
    return NULL;
}

//
// Start worker threads
//
void request_init_workers(int num) {
    srand(time(NULL)); // Seed once

    for (int i = 0; i < num; i++) {
        pthread_t tid;
        if (pthread_create(&tid, NULL, thread_request_worker, NULL) != 0) {
            perror("pthread_create");
            exit(1);
        }
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
}

//
// Return 1 if static, 0 if dynamic content
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
        ptr = strchr(uri, '?');
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
// Serves static content
//
void request_serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];
    
    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);
    
    // Memory-map the file
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);
    
    // Build and send response headers
    sprintf(buf, ""
        "HTTP/1.0 200 OK\r\n"
        "Server: OSTEP WebServer\r\n"
        "Content-Length: %d\r\n"
        "Content-Type: %s\r\n\r\n", 
        filesize, filetype);
    
    write_or_die(fd, buf, strlen(buf));
    
    // Send file content
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}

//
// Initial handling of the request (called from main server loop)
//
void request_handle(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];
    
    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);
    
    if (strcasecmp(method, "GET")) {
        request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
        return;
    }

    request_read_headers(fd);
    is_static = request_parse_uri(uri, filename, cgiargs);

    if (stat(filename, &sbuf) < 0) {
        request_error(fd, filename, "404", "Not found", "server could not find this file");
        return;
    }

    if (is_static) {
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            request_error(fd, filename, "403", "Forbidden", "server could not read this file");
            return;
        }

        // Directory traversal protection
        char resolved[MAXBUF], safe_base[MAXBUF];
        realpath("./files", safe_base);
        realpath(filename, resolved);
        if (strncmp(resolved, safe_base, strlen(safe_base)) != 0) {
            request_error(fd, filename, "403", "Forbidden", "illegal directory access");
            return;
        }

        enqueue_request(fd, resolved, sbuf.st_size);
    } else {
        request_error(fd, filename, "501", "Not Implemented", "server does not serve dynamic content request");
    }
}

