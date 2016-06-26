/* CSci4061
* section: 5
* date: 03/21/16
* name: Zach Foster
* id: foste448 */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "util.h"

#define MAX_THREADS 100
#define MAX_QUEUE_SIZE 100
#define MAX_REQUEST_LENGTH 64
#define MAX_PATH_LENGTH 4096

// Structure for queue.
typedef struct request_queue {
    int m_socket;
    char m_szRequest[MAX_REQUEST_LENGTH];
} request_queue_t;

/* GLOBALS */
// Queue, log, dispatcher status
FILE *request_log;
request_queue_t *queue;
int queue_size;
int queue_idx = -1;
int dispatchers_done = 0;

// Mutex and CVs
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t get_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t queue_full_cv = PTHREAD_COND_INITIALIZER;
pthread_cond_t queue_empty_cv = PTHREAD_COND_INITIALIZER;

/*
 * The dispatch function is called by the dispatch threads. The dispatch threads call accept_connection
 * and assign the return value to m_socket of the request. If the return value is < 0, it is ignored.
 * get_request is then called (under a lock because it isn't thread safe). If get_request didn't return 0,
 * it is ignored and the loop continues. The dispatch thread then adds the completed request struct to the
 * queue with CVs for synchronization.
 */

void * dispatch(void * arg) {
    while(1){
        int get;
        request_queue_t request;
        if((request.m_socket = accept_connection()) < 0) {
            // If m_socket is < 0, the dispatcher needs to exit. 
            // pthread_exit(NULL);
            // NOTE - Forums have stated that we should simply IGNORE when this fails.
            continue;
        }
        
        pthread_mutex_lock(&get_mutex);
        get = get_request(request.m_socket, request.m_szRequest);
        pthread_mutex_unlock(&get_mutex);
        
        if(get != 0) {
            printf("Faulty request.\n");
            continue;
        } else {
            // Make sure queue isn't full, wait if it is.
            pthread_mutex_lock(&queue_mutex);
            while(queue_idx == queue_size){
                pthread_cond_wait(&queue_full_cv, &queue_mutex);
            }
            // Once queue is not full, insert request
            ++queue_idx;
            queue[queue_idx] = request;
            // Signal so the workers know the queue isn't empty. Unlock queue mutex.
            pthread_cond_signal(&queue_empty_cv);
            pthread_mutex_unlock(&queue_mutex);
        }
    }
    return NULL;
}


/* The worker function is called by the worker threads. The worker threads wait for
 * requests to be in the queue. When there is a request they obtain a lock on the queue
 * and copy out a request. They then locate the requested file, figure out the file type,
 * figure out how many bytes the file is, and call return_result. If the file cannot be
 * located, return_error is called. The worker writes to the log after calling return_result
 * or return_error.
 */
void * worker(void * arg) {
    int request_count = 0; // for logging.
    while(1){
        long id = (long)arg; // for logging.
        request_queue_t request;
        char content_type[16];
        int fd;
        int request_length;

        pthread_mutex_lock(&queue_mutex);
        // If queue is empty, either exit if dispatchers are finished, or wait.
        // NOTE - This will never happen because the dispatcher threads never 
        // exit (per further instruction).
        while(queue_idx == -1){
            if(dispatchers_done){
                pthread_mutex_unlock(&queue_mutex);
                pthread_exit(NULL);
            }
            pthread_cond_wait(&queue_empty_cv, &queue_mutex);
        }

        // Remove request from queue.
        request = queue[queue_idx];
        // Set new queue index.
        --queue_idx;
        // Signal that the queue is not full, unlock queue mutex.
        pthread_cond_signal(&queue_full_cv);
        pthread_mutex_unlock(&queue_mutex);

        // Increment request_count for logging.
        ++request_count;

        // Open file, create buffer
        char filepath[MAX_PATH_LENGTH];
        strcpy(filepath, ".");
        strcat(filepath, request.m_szRequest);
        if((fd = open(filepath, O_RDONLY)) != -1){
            // Begin parsing content type.
            request_length = strlen(request.m_szRequest);
            // Compare end of strings to file extensions, set content_type.
            if (strcmp(".html", request.m_szRequest + request_length - 5) == 0){
                strcpy(content_type, "text/html");
            } else if (strcmp(".gif", request.m_szRequest + request_length - 4) == 0){
                strcpy(content_type, "image/gif");
            } else if (strcmp(".jpg", request.m_szRequest + request_length - 4) == 0){
                strcpy(content_type, "image/jpeg");
            } else {
                strcpy(content_type, "text/plain");
            }
            // Get file size w/ fstat and allocate a buf for return_result.
            struct stat stat;
            if(fstat(fd, &stat) < 0) {
                perror("fstat failed.\n");
                char fstat_error[32];
                strcpy(fstat_error, "Byte counting error.");
                if((return_error(request.m_socket, fstat_error)) < 0) {
                    printf("Error in return_error\n");
                }

                // Log failure
                pthread_mutex_lock(&log_mutex);
                fprintf(request_log, "[%ld][%d][%d][%s][%s]\n", id, request_count, request.m_socket,
                        request.m_szRequest, fstat_error);
                fflush(request_log);
                pthread_mutex_unlock(&log_mutex);
                continue;
            }

            // Initialize buf, used char * because thats what return_result uses.
            char *buf = malloc(stat.st_size);
            // Read file into buffer, close file.
            if((read(fd, buf, stat.st_size)) < 0) {
                perror("Error reading requested file into buffer.\n");
                char read_error[32];
                strcpy(read_error, "Read error.");
                if((return_error(request.m_socket, read_error)) < 0) {
                    printf("Error in return_error\n");
                }
                // Log failure
                pthread_mutex_lock(&log_mutex);
                fprintf(request_log, "[%ld][%d][%d][%s][%s]\n", id, request_count, request.m_socket,
                        request.m_szRequest, read_error);
                fflush(request_log);
                pthread_mutex_unlock(&log_mutex);
                close(fd);
                free(buf);
                continue;
            }
            close(fd);

            // Call return result.
            if((return_result(request.m_socket, content_type, buf, stat.st_size)) < 0) {
                printf("Error in return_result\n");
            }

            // Log success
            pthread_mutex_lock(&log_mutex);
            fprintf(request_log, "[%ld][%d][%d][%s][%lld]\n", id, request_count, request.m_socket,
                    request.m_szRequest, stat.st_size);
            fflush(request_log);
            pthread_mutex_unlock(&log_mutex);

            // Cleanup
            free(buf);
        } else {
            char fnf_error[32];
            strcpy(fnf_error, "File not found.");
            if((return_error(request.m_socket, fnf_error)) < 0) {
                printf("Error in return_error\n");
            }

            // Log failure
            pthread_mutex_lock(&log_mutex);
            fprintf(request_log, "[%ld][%d][%d][%s][%s]\n", id, request_count, request.m_socket,
                    request.m_szRequest, fnf_error);
            fflush(request_log);
            pthread_mutex_unlock(&log_mutex);
        }
    }
    return NULL;
}

/*
 * The main function parses command line arguments, changes the working directory, opens the log file,
 * starts the dispatcher and worker threads, and joins the dispatcher and worker threads when they are
 * finished. Finally, the main function cleans up mutexes and CVs, and closes the log file.
 */
int main(int argc, char **argv) {
    int port, num_dispatchers, num_workers;
    char path[MAX_PATH_LENGTH];
    int i, create_error;

    // Make sure correct amount of arguments
    if(argc != 6) {
        printf("usage: %s port path num_dispatchers num_workers queue_length\n", argv[0]);
        return -1;
    }

    // Assign and check arguments.
    if((port = atoi(argv[1])) < 1025 || port > 65535){
        printf("Port number: %d is not valid. Please choose a port between 1025 and 65535\n", port);
        return -1;
    }
    strcpy(path, argv[2]);
    if((num_dispatchers = atoi(argv[3])) < 1 || num_dispatchers > MAX_THREADS) {
        printf("Number of dispatcher threads must be at least one and at most %d.\n", MAX_THREADS);
        return -1;
    }
    if((num_workers = atoi(argv[4])) < 1 || num_workers > MAX_THREADS) {
        printf("Number of worker threads must be at least one and at most %d.\n", MAX_THREADS);
        return -1;
    }
    if((queue_size = atoi(argv[5])) < 1 || queue_size > MAX_QUEUE_SIZE) {
        printf("Queue length must be at least one and at most %d.\n", MAX_QUEUE_SIZE);
        return -1;
    }

    // Change directory to path so we can use just use . as web root.
    if(chdir(path) != 0){
        perror("Couldn't chdir to supplied path.\n");
        return -1;
    }

    // Open log file for writing.
    if((request_log = fopen("web_server_log", "w")) < 0) {
        perror("Failed to open log file.\n");
    }

    // Create thread and arg arrays. malloc queue.
    pthread_t dispatchers[num_dispatchers];
    pthread_t workers[num_workers];
    long dispatcher_args[num_dispatchers];
    long worker_args[num_workers];
    queue = malloc(sizeof(request_queue_t) * queue_size);

    // Call init and launch threads.
    init(port);
    for(i = 0; i < num_dispatchers; ++i){
        dispatcher_args[i] = i;
        if((create_error = pthread_create(&dispatchers[i], NULL, dispatch, (void *) dispatcher_args[i]))){
            // Print thread and error number if problem creating thread. Retry if insufficient resources.
            // NOTE - I emailed about this and received no response. If there is a better way to handle EAGAIN,
            // please let me know, but I feel it would be unfair to lose points for this since I asked.
            if(create_error != EAGAIN){
                printf("Error creating dispatcher thread number %d. Error number: %d.\n", i, create_error);
            }
            while(create_error == EAGAIN){
                create_error = pthread_create(&dispatchers[i], NULL, dispatch, (void *) dispatcher_args[i]);
            }
        }
    }
    for(i = 0; i < num_workers; ++i){
        worker_args[i] = i;
        if((create_error = pthread_create(&workers[i], NULL, worker, (void *) worker_args[i]))){
            // Print thread and error number if problem creating thread. Retry if insufficient resources.
            // NOTE - I emailed about this and received no response. If there is a better way to handle EAGAIN,
            // please let me know, but I feel it would be unfair to lose points for this since I asked.
            if(create_error != EAGAIN){
                printf("Error creating worker thread number %d. Error number: %d.\n", i, create_error);
            }
            while(create_error == EAGAIN){
                create_error = pthread_create(&workers[i], NULL, worker, (void *) worker_args[i]);
            }
        }
    }

    // Wait for dispatchers to finish by joining them.
    for(i = 0; i < num_dispatchers; ++i){
        if(pthread_join(dispatchers[i], NULL))
            printf("Error joining dispatcher thread number: %d.\n", i);
    }

    // Set global so worker threads know that all dispatchers have exited.
    dispatchers_done = 1;
    
    // Wait for workers by joining.
    for(i = 0; i < num_workers; ++i){
        if(pthread_join(workers[i], NULL))
            printf("Error joining worker thread number: %d.\n", i);
    }

    // Cleanup
    pthread_mutex_destroy(&log_mutex);
    pthread_mutex_destroy(&queue_mutex);
    pthread_mutex_destroy(&get_mutex);
    pthread_cond_destroy(&queue_full_cv);
    pthread_cond_destroy(&queue_empty_cv);
    free(queue);
    if(fclose(request_log) < 0) {
        perror("Failed to close log file.\n");
    }

    return 0;
}
