#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdatomic.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "config.h"
#include "logger.h"
#include "resp.h"
#include "kvstore.h"
#include "auth.h"
#include "workqueue.h"

#include <stdbool.h>
#include <pthread.h>

#define MAX_ACCEPT_LEN 128

static work_queue queue;
static pthread_t *workers = NULL;
static int workers_count = 0;

pthread_rwlock_t kv_lock = PTHREAD_RWLOCK_INITIALIZER;
static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t shutdown_requested = 0;


static pthread_mutex_t clients_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_t *client_threads = NULL;
static int client_threads_count = 0;
static int client_threads_cap = 0;
static int active_clients = 0;

static pthread_mutex_t tasks_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  task_done_cv  = PTHREAD_COND_INITIALIZER;
static long long active_tasks = 0;


typedef struct _client {
    int fd;

    bool authenticated;
    bool should_close;
    pthread_mutex_t write_mtx;
    _Atomic int refcnt;

    char username[64];
} _client;

static void client_init(_client *c, int fd)
{
    c->fd = fd;
    c->authenticated = false;
    c->should_close = false;
    atomic_init(&c->refcnt, 1);
    c->username[0] = '\0';
    pthread_mutex_init(&c->write_mtx, NULL);
}

static void client_destroy(_client *c){
    pthread_mutex_destroy(&c->write_mtx);
}

static inline void client_acquire(_client *c) {
    atomic_fetch_add_explicit(&c->refcnt, 1, memory_order_relaxed);
}

static void client_release(_client *c) {
    if (atomic_fetch_sub_explicit(&c->refcnt, 1, memory_order_acq_rel) == 1) {
        close(c->fd);
        client_destroy(c);
        free(c);
    }
}

static void task_cleanup(void *arg) {
    task *t = (task*)arg;
    if (t == NULL) {
        return;
    }

    client_release(t->client);
    resp_free_command(&t->cmd);
    free(t);

    pthread_mutex_lock(&tasks_mtx);
    active_tasks--;
    pthread_cond_broadcast(&task_done_cv);
    pthread_mutex_unlock(&tasks_mtx);
}


static void client_thread_cleanup(void *arg) {
    _client *c = arg;

    pthread_mutex_lock(&clients_mtx);
    active_clients--;
    pthread_mutex_unlock(&clients_mtx);

    pthread_mutex_lock(&tasks_mtx);
    pthread_cond_broadcast(&task_done_cv);
    pthread_mutex_unlock(&tasks_mtx);

    client_release(c);
}


typedef struct arg_reader {
    _client *client;
} arg_reader;

static void *client_reader_thread(void *arg){

    arg_reader *ra = (arg_reader*)arg;
    _client *client = ra->client;
    free(ra);

    pthread_cleanup_push(client_thread_cleanup, client);

    resp_buffer rbuf;
    resp_buffer_init(&rbuf);

    while (running && !client->should_close) {
        char tmp[1024];
        ssize_t n = read(client->fd, tmp, sizeof(tmp));
        if (n == 0) break;
        if (n == -1) {
            if (errno == EINTR) continue;
            break;
        }

        int res = resp_buffer_append(&rbuf, tmp, (size_t)n);
        if (res == -1) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_error(client->fd, "ERR input buffer overflow");
            pthread_mutex_unlock(&client->write_mtx);
            break;
        }

        while (!client->should_close) {
            resp_command cmd;
            int pr = resp_try_parse_command(&rbuf, &cmd);
            if (pr == 0){
                break;
            }
            if (pr == -1) {
                pthread_mutex_lock(&client->write_mtx);
                resp_send_error(client->fd, "ERR protocol error");
                pthread_mutex_unlock(&client->write_mtx);
                client->should_close = true;
                break;
            }

            task *t = (task*)calloc(1, sizeof(task));
            if (t == NULL) {
                resp_free_command(&cmd);
                pthread_mutex_lock(&client->write_mtx);
                resp_send_error(client->fd, "ERR no memory");
                pthread_mutex_unlock(&client->write_mtx);
                break;
            }

            client_acquire(client);
            t->client = client;
            t->cmd = cmd;

            res = wq_push(&queue, t);
            if (res < 0) {
                resp_free_command(&t->cmd);
                client_release(client); 
                free(t);
                break;
            }

        }
    }
    pthread_cleanup_pop(1);

    return NULL;
}


static void handle_signal(int sig){
    shutdown_requested = 1;
    running = 0;
}

void tolower_command(char *s){
    if (s == NULL){
        return;
    }

    for (char *p = s; *p != '\0'; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
}

static void handle_resp_command(_client *client, resp_command *cmd){
    if (cmd->argc < 1) {
        pthread_mutex_lock(&client->write_mtx);
        resp_send_error(client->fd, "empty command");
        pthread_mutex_unlock(&client->write_mtx);
        return;
    }

    char *cmd_type = cmd->argv[0];

    tolower_command(cmd_type);

    int is_ping  = (strcmp(cmd_type, "ping")  == 0) ? 1 : 0;
    int is_auth  = (strcmp(cmd_type, "auth")  == 0) ? 1 : 0;
    int is_hello = (strcmp(cmd_type, "hello") == 0) ? 1 : 0;
    int is_quit  = (strcmp(cmd_type, "quit")  == 0) ? 1 : 0;

    if (!client->authenticated && !is_ping && !is_auth && !is_hello && !is_quit) {
        LOG_ERROR("please, authentoficate");
        pthread_mutex_lock(&client->write_mtx);
        resp_send_error(client->fd, "Not authorized");
        pthread_mutex_unlock(&client->write_mtx);
        return;
    }

    if (is_ping) {
        pthread_mutex_lock(&client->write_mtx);
        resp_send_simple_string(client->fd, "PONG");
        pthread_mutex_unlock(&client->write_mtx);
        return;
    }

    if (is_hello) {
        pthread_mutex_lock(&client->write_mtx);
        resp_send_simple_string(client->fd, "OK");
        pthread_mutex_unlock(&client->write_mtx);
        return;
    }

    if (is_auth) {
        if (cmd->argc < 3) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_error(client->fd, "wrong number of arguments for 'AUTH'");
            pthread_mutex_unlock(&client->write_mtx);
            return;
        }

        const char *user = cmd->argv[1];
        const char *pass = cmd->argv[2];

        int ok = auth_check(user, pass);
        if (!ok) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_error(client->fd, "invalid username-password pair");
            pthread_mutex_unlock(&client->write_mtx);
            return;
        }

        client->authenticated = 1;
        strncpy(client->username, user, sizeof(client->username));
        client->username[sizeof(client->username) - 1] = '\0';

        pthread_mutex_lock(&client->write_mtx);
        resp_send_simple_string(client->fd, "OK");
        pthread_mutex_unlock(&client->write_mtx);
        return;
    }

    if (is_quit) {
        pthread_mutex_lock(&client->write_mtx);
        resp_send_simple_string(client->fd, "OK");
        pthread_mutex_unlock(&client->write_mtx);
        client->should_close = true;
        shutdown(client->fd, SHUT_RDWR);
        return;
    }

    if (strcmp(cmd_type, "set") == 0) {

        if (cmd->argc < 3) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_error(client->fd, "wrong number of arguments for 'SET'");
            pthread_mutex_unlock(&client->write_mtx);
            return;
        }

        const char *key = cmd->argv[1];
        const char *value = cmd->argv[2];

        if (key == NULL){
            LOG_DEBUG("key set to null");
        }
        LOG_DEBUG("key set to not null");

        pthread_rwlock_wrlock(&kv_lock);
        int rc = kv_set(key, value);
        pthread_rwlock_unlock(&kv_lock);

        if (rc != 0) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_error(client->fd, "error in SET command");
            pthread_mutex_unlock(&client->write_mtx);
            return;
        }

        pthread_mutex_lock(&client->write_mtx);
        resp_send_simple_string(client->fd, "OK");
        pthread_mutex_unlock(&client->write_mtx);
        return;
    }

    if (strcmp(cmd_type, "get") == 0) {

        if (cmd->argc < 2) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_error(client->fd, "wrong number of arguments for 'GET'");
            pthread_mutex_unlock(&client->write_mtx);
            return;
        }

        const char *key = cmd->argv[1];
        char *value = NULL;
        pthread_rwlock_rdlock(&kv_lock);
        int rc = kv_get(key, &value);
        pthread_rwlock_unlock(&kv_lock);

        if (rc == -1) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_error(client->fd, "error in GET command");
            pthread_mutex_unlock(&client->write_mtx);
            return;
        }

        if (rc == 0) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_null_bulk(client->fd);
            pthread_mutex_unlock(&client->write_mtx);
        } 
        if (rc == 1) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_bulk_string(client->fd, value);
            pthread_mutex_unlock(&client->write_mtx);
            free(value);
        }
        return;
    }

    if (strcmp(cmd_type, "del") == 0) {
        if (cmd->argc < 2) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_error(client->fd, "wrong number of arguments for 'DEL'");
            pthread_mutex_unlock(&client->write_mtx);
            return;
        }

        pthread_rwlock_wrlock(&kv_lock);
        int deleted = kv_del(cmd->argv[1]);
        pthread_rwlock_unlock(&kv_lock);

        pthread_mutex_lock(&client->write_mtx);
        resp_send_integer(client->fd, deleted);
        pthread_mutex_unlock(&client->write_mtx);
        return;
    }

    if (strcmp(cmd_type, "expire") == 0) {
        if (cmd->argc < 3) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_error(client->fd, "wrong number of arguments for 'EXPIRE'");
            pthread_mutex_unlock(&client->write_mtx);
            return;
        }

        int timeout = atoi(cmd->argv[2]);
        if (timeout < 0) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_integer(client->fd, 0);
            pthread_mutex_unlock(&client->write_mtx);
            return;
        }

        pthread_rwlock_wrlock(&kv_lock);
        int res = kv_expire(cmd->argv[1], timeout);
        pthread_rwlock_unlock(&kv_lock);

        pthread_mutex_lock(&client->write_mtx);
        resp_send_integer(client->fd, res);
        pthread_mutex_unlock(&client->write_mtx);
        return;
    }

    if (strcmp(cmd_type, "ttl") == 0) {
        if (cmd->argc < 2) {
            pthread_mutex_lock(&client->write_mtx);
            resp_send_error(client->fd, "wrong number of arguments for 'TTL'");
            pthread_mutex_unlock(&client->write_mtx);
            return;
        }

        pthread_rwlock_rdlock(&kv_lock);
        int ttl = kv_ttl(cmd->argv[1]);
        pthread_rwlock_unlock(&kv_lock);

        pthread_mutex_lock(&client->write_mtx);
        resp_send_integer(client->fd, ttl);
        pthread_mutex_unlock(&client->write_mtx);
        return;
    }
    pthread_mutex_lock(&client->write_mtx);
    resp_send_error(client->fd, "unknown command");
    pthread_mutex_unlock(&client->write_mtx);
}

static void *worker_thread(void *arg){
    (void)arg;

    while(1) {
        task *t = wq_pop(&queue);
        if (t==NULL) break; 

        pthread_mutex_lock(&tasks_mtx);
        active_tasks++;
        pthread_mutex_unlock(&tasks_mtx);

        pthread_cleanup_push(task_cleanup, t);

        _client *client = t->client;

        handle_resp_command(client, &t->cmd);

        pthread_cleanup_pop(1);

    }

    return NULL;
}

void create_signal_handler(){
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}



int main(int argc, char **argv){
    repa_config cfg;
    config_set_defaults(&cfg);
    char *config_path = (char*)"repa.conf";

    int show_help = 0;
    int cli_res = config_apply_cli_args(&cfg, argc, argv, &config_path, &show_help);
    if (cli_res != 0) {
        if (show_help) {
            return 0;
        }
        return 1;
    }

    if (config_path != NULL) {
        if (config_load_file(&cfg, config_path) != 0) {
            fprintf(stderr,"Warning: could not load config file %s - using defaults\n",config_path);
        }
    }

    cli_res = config_apply_cli_args(&cfg, argc, argv, NULL, NULL);
    if (cli_res < 0) {
        return 1;
    }

    int init_res = logger_init(cfg.log_output, cfg.log_level);
    if (init_res < 0) {
        fprintf(stderr,"Warning: failed to open log file '%s', logging to stderr\n",cfg.log_output);
    }

    init_res = kv_init(cfg.default_ttl);
    if (init_res < 0) {
        LOG_ERROR("Failed to init kvstore");
        logger_shutdown();
        return 1;
    }

    init_res = auth_init(cfg.default_user, cfg.default_password);
    if (init_res < 0) {
        LOG_ERROR("Failed to init auth module");
        kv_shutdown();
        logger_shutdown();
        return 1;
    }

    LOG_INFO("=== Repa starting ===");

    wq_init(&queue);

    workers_count = cfg.workers;
    if (workers_count <= 0) {
        workers_count = 1;
    }
    workers = calloc((size_t)workers_count, sizeof(pthread_t));
    if (workers == NULL) {
        wq_destroy(&queue);
        kv_shutdown();
        logger_shutdown();
        return 1;
    }

    for (int i = 0; i < workers_count; i++) {
        int create_res = pthread_create(&workers[i], NULL, worker_thread, NULL);
        if (create_res != 0) {
            LOG_ERROR("Failed to create worker thread");
            wq_shutdown(&queue);
            for (int j = 0; j < i; j++){
                pthread_join(workers[j], NULL);
            } 
            free(workers);
            workers = NULL;
            wq_destroy(&queue);
            kv_shutdown();
            logger_shutdown();
            return 1;
        }
    }

    
    create_signal_handler();

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        LOG_ERROR("Failed to create socket");
        
        wq_shutdown(&queue);
        if(workers != NULL){
            for (int i = 0; i < workers_count; i++) {
                pthread_join(workers[i], NULL);
            }
            free(workers);
            workers = NULL;
        }
        wq_destroy(&queue);

        logger_shutdown();
        kv_shutdown();
        return 1;
    }

    int optval = 1;
    int res = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (res < 0) {
        perror("setsockopt");
        LOG_ERROR("Failed to set SO_REUSEADDR");
        close(listen_fd);

        wq_shutdown(&queue);
        if(workers != NULL){
            for (int i = 0; i < workers_count; i++) {
                pthread_join(workers[i], NULL);
            }
            free(workers);
            workers = NULL;
        }
        wq_destroy(&queue);

        logger_shutdown();
        kv_shutdown();
        
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(cfg.port);

    int bind_res = bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    if (bind_res < 0) {
        perror("bind");
        LOG_ERROR("Failed to bind socket");
        close(listen_fd);

        wq_shutdown(&queue);
        if(workers != NULL){
            for (int i = 0; i < workers_count; i++) {
                pthread_join(workers[i], NULL);
            }
            free(workers);
            workers = NULL;
        }
        wq_destroy(&queue);

        logger_shutdown();
        kv_shutdown();
        return 1;
    }

    int listen_res = listen(listen_fd, MAX_ACCEPT_LEN);
    if (listen_res < 0) {
        perror("listen");
        LOG_ERROR("Failed to listen on socket");
        close(listen_fd);
        wq_shutdown(&queue);
        if(workers != NULL){
            for (int i = 0; i < workers_count; i++) {
                pthread_join(workers[i], NULL);
            }
            free(workers);
            workers = NULL;
        }
        wq_destroy(&queue);

        logger_shutdown();
        kv_shutdown();
        return 1;
    }

    LOG_INFO("Repa is now listening for TCP connections");




    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_size = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_size);
        if (client_fd < 0) {
            if (errno == EINTR) {
                if (!running) break;
                continue;
            }
            perror("accept");
            LOG_ERROR("accept() failed");
            break;
        }

        LOG_INFO("Accepted new connection");

        _client *c = (_client*)calloc(1, sizeof(_client));
        if (c == NULL) {
            LOG_ERROR("no memory for client");
            close(client_fd);
            continue;
        }

        client_init(c, client_fd);

        arg_reader *ra = (arg_reader*) calloc(1, sizeof(arg_reader));
        if (ra == NULL) {
            LOG_ERROR("no memory for reader args");
            close(client_fd);
            client_destroy(c);
            free(c);
            continue;
        }
        ra->client = c;

        pthread_t read_th;
        int res = pthread_create(&read_th, NULL, client_reader_thread, ra);
        if (res != 0) {
            LOG_ERROR("pthread_create reader failed");
            close(client_fd);
            free(ra);
            client_destroy(c);
            free(c);
            continue;
        }
        pthread_mutex_lock(&clients_mtx);

        if (client_threads_count == client_threads_cap) {
            int new_cap =0;
            if (client_threads_cap == 0){
                new_cap = 16;
            }else{
                new_cap  = client_threads_cap * 2;
            }
            pthread_t *tmp = realloc(client_threads, (size_t)new_cap * sizeof(pthread_t));
            if (tmp != NULL) {
                client_threads = tmp;
                client_threads_cap = new_cap;
            } else {
                LOG_ERROR("realloc for client_threads failed");
            }
        }
        if (client_threads != NULL && client_threads_count < client_threads_cap) {
            client_threads[client_threads_count] = read_th;
            client_threads_count++;
        }

        active_clients++;

        pthread_mutex_unlock(&clients_mtx);

    }

    close(listen_fd);
    wq_shutdown(&queue);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;

    int done_tasks =0;
    int done_clients = 0;
    pthread_mutex_lock(&tasks_mtx);
    while(!done_tasks || !done_clients) {
        if(active_tasks == 0){
            done_tasks =1;
        }

        pthread_mutex_lock(&clients_mtx);
        if (active_clients == 0){
            done_clients =1;
        }
        pthread_mutex_unlock(&clients_mtx);

        if (done_tasks && done_clients) break;

        int rc = pthread_cond_timedwait(&task_done_cv, &tasks_mtx, &ts);
        if (rc == ETIMEDOUT) break;
    }
    pthread_mutex_unlock(&tasks_mtx);

    int need_cancel = 0;

    pthread_mutex_lock(&tasks_mtx);
    if (active_tasks != 0){
        need_cancel = 1;
    }
    pthread_mutex_unlock(&tasks_mtx);

    pthread_mutex_lock(&clients_mtx);
    if (active_clients != 0){
        need_cancel = 1;
    } 
    pthread_mutex_unlock(&clients_mtx);

    if (need_cancel) {
        LOG_WARN("Timeout waiting for threads, cancelling...");

        pthread_mutex_lock(&clients_mtx);
        for (int i = 0; i < client_threads_count; i++) {
            pthread_cancel(client_threads[i]);
        }
        pthread_mutex_unlock(&clients_mtx);

        for (int i = 0; i < workers_count; i++) {
            pthread_cancel(workers[i]);
        }
    }


    LOG_INFO("Repa shutting down (TCP server stopped)");


    pthread_t *to_join = NULL;
    int to_join_n = 0;

    pthread_mutex_lock(&clients_mtx);

    to_join = client_threads;
    to_join_n = client_threads_count;

    client_threads = NULL;
    client_threads_count = 0;
    client_threads_cap = 0;

    pthread_mutex_unlock(&clients_mtx);

    for (int i = 0; i < to_join_n; i++) {
        pthread_join(to_join[i], NULL);
    }

    free(to_join);
    to_join = NULL;

    if(workers != NULL){
        for (int i = 0; i < workers_count; i++) {
            pthread_join(workers[i], NULL);
        }
        free(workers);
        workers = NULL;
    }
    wq_destroy(&queue);  
    
    kv_shutdown();
    LOG_INFO("All threads have finished");
    LOG_INFO("Repa finished");

    logger_shutdown();

    

    return 0;

}   
