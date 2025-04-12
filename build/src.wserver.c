pthread_t thread_pool[num_threads];
for(int i=0; i<num_threads; i++)
    pthread_create(&thread_pool[i], NULL, thread_request_serve_static, NULL);
