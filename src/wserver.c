#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h>
#include "request.h"
#include "io_helper.h"

#define MAXLINE 1024

typedef struct {
	int conn_fd;
	off_t file_size;
} RequestInfo;

char default_root[] = ".";
RequestInfo* buffer = NULL;
int count = 0;
int use_index = 0;
int fill_index = 0;
int buffer_size = 1;
char* scheduling_algorithm = "FIFO";
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t fill = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void put_buffer(RequestInfo request) {
    buffer[fill_index] = request;
    fill_index = (fill_index + 1) % buffer_size;
    count++;
}

RequestInfo get_buffer() {
    RequestInfo res;
    if (strcmp(scheduling_algorithm, "SFF") == 0) {
        int min_index = use_index;
        for (int i = 1; i < count; i++) {
            int curr_index = (use_index + i) % buffer_size;
            if (buffer[curr_index].file_size < buffer[min_index].file_size) {
                min_index = curr_index;
            }
        }
        res = buffer[min_index];
        use_index = (min_index + 1) % buffer_size;
    } else {
        res = buffer[use_index];
        use_index = (use_index + 1) % buffer_size;
    }
    count--;
    return res;
}

void* connection_handler(void* arg) {
    printf("Running thread %ld\n", pthread_self());
    while (1) {
        pthread_mutex_lock(&mutex);
        while (count == 0) {
            pthread_cond_wait(&fill, &mutex);
        }

        RequestInfo request = get_buffer();
        pthread_cond_signal(&empty);
        pthread_mutex_unlock(&mutex);
        request_handle(request.conn_fd);
        close_or_die(request.conn_fd);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    int c;
    char* root_dir = default_root;
    int port = 10000;
    int threads = 1;

    while ((c = getopt(argc, argv, "d:p:t:b:s:")) != -1) {
        switch (c) {
            case 'd':
                root_dir = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 't':
                threads = atoi(optarg);
                break;
            case 'b':
                buffer_size = atoi(optarg);
                break;
            case 's':
                scheduling_algorithm = optarg;
                break;
            default:
                fprintf(stderr, "usage: wserver [-d basedir] [-p port] [-t threads] [-b buffer_size] [-s scheduling_algorithm]\n");
                exit(1);
        }
    }

    chdir_or_die(root_dir);
    buffer = (RequestInfo*)malloc(sizeof(RequestInfo) * buffer_size);
    pthread_t thread_ids[threads];
    for (int i = 0; i < threads; i++) {
        pthread_create(&thread_ids[i], NULL, connection_handler, NULL);
    }

    int listen_fd = open_listen_fd_or_die(port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int conn_fd = accept_or_die(listen_fd, (sockaddr_t*)&client_addr, &client_len);

        char filename[MAXLINE];
        if (parse_request(conn_fd, filename) < 0) {
            close_or_die(conn_fd);
            continue;
        }

        struct stat file_stat;
        if (stat(filename, &file_stat) < 0) {
            close_or_die(conn_fd);
            continue;
        }

        RequestInfo request;
        request.conn_fd = conn_fd;
        request.file_size = file_stat.st_size;

        pthread_mutex_lock(&mutex);

        while (count == buffer_size) {
            pthread_cond_wait(&empty, &mutex);
        }
        put_buffer(request);
        pthread_cond_signal(&fill);
        pthread_mutex_unlock(&mutex);
    }

    return 0;
}
