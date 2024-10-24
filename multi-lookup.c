#include "multi-lookup.h"

void *resolvers_func(void *arg) {
    // grab arguments
    req_res_arg_t *res_arg = (req_res_arg_t *) arg;

    // count and time each file
    unsigned int num_host_processed = 0;
    struct timeval start;
    gettimeofday(&start, NULL);

    char *hostname = malloc(MAX_NAME_LENGTH * sizeof(char));

    while (1) { // while there are file's available
        
        array_get(res_arg->arr, &hostname);
        hostname[strcspn(hostname, "\r\n")] = '\0';

        if (strncmp(hostname, "DUMMY_SIGKILL", strlen(hostname)) == 0) {
            if (res_arg->arr->size > 0) {
                array_get(res_arg->arr, &hostname);
                hostname[strcspn(hostname, "\r\n")] = '\0';
                array_put(res_arg->arr, "DUMMY_SIGKILL");
            } else {
                array_put(res_arg->arr, "DUMMY_SIGKILL");
                break;
            }
        }

        // resolve DNS
        char dns[MAX_IP_LENGTH+1];

        if (dnslookup(hostname, dns, MAX_IP_LENGTH+1)) {
            strcpy(dns, "NOT_RESOLVED");
        }

        sem_wait(&res_arg->main_arg->resolver_log_mutex);
            fprintf(res_arg->main_arg->resolver_log, "%s, %s\n", hostname, dns);
        sem_post(&res_arg->main_arg->resolver_log_mutex);
        
        num_host_processed++;
    }

    // free buffer
    free(hostname);

    struct timeval end;
    gettimeofday(&end, NULL);
    float tot = end.tv_usec - start.tv_usec;
    tot /= 1000000.0; // convert to seconds

    // print output
    printf("thread %ld resolved %d hosts in %f seconds\n", pthread_self(), num_host_processed, tot);

    return NULL;
}

void *requesters_func(void *arg) {
    // grab arguments
    req_res_arg_t *req_arg = (req_res_arg_t *) arg;

    // count and time each file
    unsigned int num_files_processed = 0;
    struct timeval start;
    gettimeofday(&start, NULL);

    // get next file name
    FILE *curr_file;

    while (1) { // while there are file's available
        sem_wait(&req_arg->main_arg->mutex);
            // if we've used all file names exit loop
            if (req_arg->main_arg->req_data_curr >= req_arg->main_arg->data_end) {
                sem_post(&req_arg->main_arg->mutex);
                break;
            }
            
            // acquire next file name
            if (access(req_arg->argv[req_arg->main_arg->req_data_curr], F_OK) != 0) {
                sem_post(&req_arg->main_arg->mutex);
                printf("invalid file %s\n", req_arg->argv[req_arg->main_arg->req_data_curr++]);
                continue;
            }
            curr_file = fopen(req_arg->argv[req_arg->main_arg->req_data_curr++], "r");
        sem_post(&req_arg->main_arg->mutex);
    
        // work on file
        char hostname[MAX_NAME_LENGTH];
        while (fgets(hostname, sizeof(hostname), curr_file)) {
            // push file on the shared array stack to be processed
            array_put(req_arg->arr, hostname);

            sem_wait(&req_arg->main_arg->requester_log_mutex);
                fprintf(req_arg->main_arg->requester_log, "%s", hostname);
            sem_post(&req_arg->main_arg->requester_log_mutex);
        }
        num_files_processed++;
        fclose(curr_file);
    }

    struct timeval end;
    gettimeofday(&end, NULL);
    float tot = end.tv_usec - start.tv_usec;
    tot /= 1000000.0; // convert to seconds

    // print output
    printf("thread %ld serviced %d files in %f seconds\n", pthread_self(), num_files_processed, tot);

    return NULL;
}

int parse_args(int argc, char** argv, main_arg_t *ret) {
    /*
        must follow this convention:
        multi-lookup <# requester> <# resolver> <requester log> <resolver log> [ <data file> ... ]
        check man page for more info
    */

    // check argc
    if (argc < 6) {
        printf("multi-lookup [m]: error, too few arguments.\n");
        return -1;
    }
    
    // num requester
    unsigned int num_requesters;
    if ( (num_requesters = atoi(argv[1])) && num_requesters <= MAX_REQUESTER_THREADS && num_requesters > 0) {
        ret->num_requester_thr = num_requesters;
    } else {
        printf("multi-lookup [m]: error parsing argument <# requester>; got: \"%s\"; expected a number between 1 and %d\n", argv[1], MAX_REQUESTER_THREADS);
        return -1;
    }

    // num resolver
    unsigned int num_resolvers;
    if ( (num_resolvers = atoi(argv[2])) && num_resolvers <= MAX_RESOLVER_THREADS && num_resolvers > 0) {
        ret->num_resolver_thr = num_resolvers;
    } else {
        printf("multi-lookup [m]: error parsing argument <# resolver>; got: \"%s\"; expected a number between 0 and %d.\n", argv[1], MAX_REQUESTER_THREADS);
        return -1;
    }

    // requester log
    if ( !(ret->requester_log = fopen(argv[3], "w")) ) { // option w: overwrite or create new, open for writing only
        printf("multi-lookup [m]: error opening/creating requester log %s.\n", argv[3]);
        return -1;
    }
    sem_init(&ret->requester_log_mutex, 0, 1);


    // resolver log
    if ( !(ret->resolver_log = fopen(argv[4], "w")) ) { // option w: overwrite or create new, open for writing only
        printf("multi-lookup [m]: error opening/creating resolver log %s.\n", argv[4]);
        return -1;
    }
    sem_init(&ret->resolver_log_mutex, 0, 1);

    // data files
    // return interval of data files
    ret->req_data_curr = 5;
    ret->data_end = argc;
    if (ret->data_end - 5 > MAX_INPUT_FILES) {
        printf("multi-lookup [m]: error, too many input files.\n");
        return -1;
    }
    sem_init(&ret->mutex, 0, 1);

    return 0;
}

int main(int argc, char **argv) {

    // parse aguments
    main_arg_t args;
    if (parse_args(argc, argv, &args) == -1)
        return -1;
    
    // init shared array
    array s;
    array_init(&s);

    // start timeer
    struct timeval start;
    gettimeofday(&start, NULL);

    // start requester threads
    pthread_t requester_threads[MAX_REQUESTER_THREADS];
    req_res_arg_t req_arg;
    req_arg.argv = argv;
    req_arg.main_arg = &args;
    req_arg.arr = &s;
    for (unsigned int i = 0; i < args.num_requester_thr; i++) {
        pthread_create(&requester_threads[i], NULL, requesters_func, &req_arg);
    }

    // start resolver threads
    pthread_t resolver_threads[MAX_RESOLVER_THREADS];
    req_res_arg_t res_arg;
    res_arg.argv = argv;
    res_arg.main_arg = &args;
    res_arg.arr = &s;
    for (unsigned int i = 0; i < args.num_resolver_thr; i++) {
        pthread_create(&resolver_threads[i], NULL, resolvers_func, &res_arg);
    }

    // rejoin both requested threads
    for (unsigned int i = 0; i < args.num_requester_thr; i++) {
        pthread_join(requester_threads[i], NULL);
    }

    // signal resolvers that requesting is done
    // array_put(&s, "DUMMY_SIGKILL");
    array_end(&s, "DUMMY_SIGKILL");

    for (unsigned int i = 0; i < args.num_resolver_thr; i++) {
        pthread_join(resolver_threads[i], NULL);
    }

    // time
    struct timeval end;
    gettimeofday(&end, NULL);
    float tot = end.tv_usec - start.tv_usec;
    tot /= 1000000.0; // convert to seconds

    printf("%s: total time is %f seconds\n", argv[0], tot);

    // clear memmory
    array_free(&s);
    fclose(args.requester_log);
    fclose(args.resolver_log);

    return 0;
}