#include "io_helper.h"
#include "request.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#define MAXBUF (8192)
#define MAX_QUEUE_SIZE 64

int num_threads = DEFAULT_THREADS;
int buffer_max_size = DEFAULT_BUFFER_SIZE;
int scheduling_algo = DEFAULT_SCHED_ALGO;

typedef struct {
    int fd;
    char filename[MAXBUF];
    int filesize;
} request_t;

request_t request_queue[MAX_QUEUE_SIZE];
int queue_front = 0, queue_back = 0, queue_count = 0;

pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t queue_not_full = PTHREAD_COND_INITIALIZER;

// Sends out HTTP response in case of errors
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];
    sprintf(body, "<!doctype html>\r\n<head><title>CYB-3053 WebServer Error</title></head>\r\n<body><h2>%s: %s</h2><p>%s: %s</p></body></html>\r\n", errnum, shortmsg, longmsg, cause);
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    write_or_die(fd, buf, strlen(buf));
    sprintf(buf, "Content-Type: text/html\r\n");
    write_or_die(fd, buf, strlen(buf));
    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
    write_or_die(fd, buf, strlen(buf));
    write_or_die(fd, body, strlen(body));
    close_or_die(fd);
}

// Reads and discards everything up to an empty text line
void request_read_headers(int fd) {
    char buf[MAXBUF];
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n")) {
        readline_or_die(fd, buf, MAXBUF);
    }
}

// Return 1 if static, 0 if dynamic content
int request_parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;
    if (!strstr(uri, "cgi")) {
        strcpy(cgiargs, "");
        sprintf(filename, ".%s", uri);
        if (uri[strlen(uri) - 1] == '/') {
            strcat(filename, "index.html");
        }
        return 1;
    } else {
        ptr = index(uri, '?');
        if (ptr) {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        } else {
            strcpy(cgiargs, "");
        }
        sprintf(filename, ".%s", uri);
        return 0;
    }
}

// Fills in the filetype given the filename
void request_get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) strcpy(filetype, "image/jpeg");
    else strcpy(filetype, "text/plain");
}

// Handles requests for static content
void request_serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];
    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);
    sprintf(buf, "HTTP/1.0 200 OK\r\nServer: OSTEP WebServer\r\nContent-Length: %d\r\nContent-Type: %s\r\n\r\n", filesize, filetype);
    write_or_die(fd, buf, strlen(buf));
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}

// Enqueue new request into buffer
void enqueue_request(int fd, char *filename, int filesize) {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == buffer_max_size)
        pthread_cond_wait(&queue_not_full, &queue_lock);
    request_queue[queue_back].fd = fd;
    strcpy(request_queue[queue_back].filename, filename);
    request_queue[queue_back].filesize = filesize;
    queue_back = (queue_back + 1) % MAX_QUEUE_SIZE;
    queue_count++;
    pthread_cond_signal(&queue_not_empty);
    pthread_mutex_unlock(&queue_lock);
}

// Dequeue request from buffer
request_t dequeue_request() {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == 0)
        pthread_cond_wait(&queue_not_empty, &queue_lock);

    int selected_index = queue_front;
    int index = queue_front;

    if (scheduling_algo == 1) {
        int min_size = request_queue[index].filesize;
        for (int i = 0, idx = queue_front; i < queue_count; i++, idx = (idx + 1) % MAX_QUEUE_SIZE) {
            if (request_queue[idx].filesize < min_size) {
                min_size = request_queue[idx].filesize;
                selected_index = idx;
            }
        }
    } else if (scheduling_algo == 2) {
        srand(time(NULL));
        int offset = rand() % queue_count;
        for (int i = 0; i < offset; i++)
            index = (index + 1) % MAX_QUEUE_SIZE;
        selected_index = index;
    }

    request_t req = request_queue[selected_index];
    for (int i = selected_index; i != queue_back; i = (i + 1) % MAX_QUEUE_SIZE) {
        int next = (i + 1) % MAX_QUEUE_SIZE;
        request_queue[i] = request_queue[next];
    }
    queue_back = (queue_back - 1 + MAX_QUEUE_SIZE) % MAX_QUEUE_SIZE;
    queue_count--;
    pthread_cond_signal(&queue_not_full);
    pthread_mutex_unlock(&queue_lock);
    return req;
}

// Thread routine for processing requests
void* thread_request_serve_static(void* arg) {
    while (1) {
        request_t req = dequeue_request();
        request_serve_static(req.fd, req.filename, req.filesize);
        close_or_die(req.fd);
    }
    return NULL;
}

// Initial handling of the request
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

        // Directory traversal mitigation
        char resolved_path[MAXBUF];
        realpath(filename, resolved_path);
        if (strstr(resolved_path, "./files") != resolved_path) {
            request_error(fd, filename, "403", "Forbidden", "Directory traversal attempt");
            return;
        }

        // Add HTTP request to buffer
        enqueue_request(fd, resolved_path, sbuf.st_size);
    } else {
        request_error(fd, filename, "501", "Not Implemented", "server does not serve dynamic content request");
    }
}
