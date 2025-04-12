typedef struct {
    int fd;
    char filename[MAXBUF];
    int filesize;
} request_t;

request_t buffer[DEFAULT_BUFFER_SIZE];
int buffer_size = 0;
int buffer_max_size = DEFAULT_BUFFER_SIZE;

pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t buffer_not_full = PTHREAD_COND_INITIALIZER;

int scheduling_algo = DEFAULT_SCHED_ALGO;

void insert_request(request_t req) {
    pthread_mutex_lock(&buffer_mutex);
    while (buffer_size >= buffer_max_size)
        pthread_cond_wait(&buffer_not_full, &buffer_mutex);

    buffer[buffer_size++] = req;

    pthread_cond_signal(&buffer_not_empty);
    pthread_mutex_unlock(&buffer_mutex);
}

request_t remove_request() {
    pthread_mutex_lock(&buffer_mutex);
    while (buffer_size == 0)
        pthread_cond_wait(&buffer_not_empty, &buffer_mutex);

    request_t req;

    if (scheduling_algo == 0) {
        req = buffer[0];
        for (int i = 1; i < buffer_size; i++)
            buffer[i - 1] = buffer[i];
    } else if (scheduling_algo == 1) {
        int min_index = 0;
        for (int i = 1; i < buffer_size; i++) {
            if (buffer[i].filesize < buffer[min_index].filesize)
                min_index = i;
        }
        req = buffer[min_index];
        for (int i = min_index + 1; i < buffer_size; i++)
            buffer[i - 1] = buffer[i];
    } else {
        int rand_index = rand() % buffer_size;
        req = buffer[rand_index];
        for (int i = rand_index + 1; i < buffer_size; i++)
            buffer[i - 1] = buffer[i];
    }

    buffer_size--;

    pthread_cond_signal(&buffer_not_full);
    pthread_mutex_unlock(&buffer_mutex);
    return req;
}

void* thread_request_serve_static(void* arg) {
    while (1) {
        request_t req = remove_request();
        request_serve_static(req.fd, req.filename, req.filesize);
        close_or_die(req.fd);
    }
}
realpath(filename, resolved_path);
char cwd[PATH_MAX];
getcwd(cwd, sizeof(cwd));
if (strncmp(cwd, resolved_path, strlen(cwd)) != 0) {
    request_error(fd, filename, "403", "Forbidden", "illegal directory access");
    return;
}

if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
    request_error(fd, filename, "403", "Forbidden", "cannot read the file");
    return;
}

request_t req;
req.fd = fd;
strcpy(req.filename, filename);
req.filesize = sbuf.st_size;

insert_request(req);
