#ifndef MULTI_LOOKUP_H
#define MULTI_LOOKUP_H

// dependencies
#include "array.h"
#include "util.h"
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>

// macros
#define MAX_INPUT_FILES 100
#define MAX_REQUESTER_THREADS 10
#define MAX_RESOLVER_THREADS 10
#define MAX_IP_LENGTH INET6_ADDRSTRLEN // from "util.h"

// structures
// for storing argument data
typedef struct {
    unsigned int num_requester_thr;
    unsigned int num_resolver_thr;

    FILE *requester_log;
    sem_t requester_log_mutex;

    FILE *resolver_log;
    sem_t resolver_log_mutex;

    unsigned int req_data_curr;
    unsigned int res_data_curr;
    unsigned int data_end;

    sem_t mutex;

} main_arg_t;

typedef struct {
    main_arg_t *main_arg;
    char** argv;
    array *arr;
} req_res_arg_t;

// main function included in this file multi-lookup.*
int main(int argc, char** argv);

// parses arguments, returns output in ret, returns success status
int parse_args(int argc, char** argv, main_arg_t *ret);

// thread method for requesters
void *requesters_func(void *arg);

// thread method for resolvers
void *resolvers_func(void *arg);




#endif // MULTI_LOOKUP_H