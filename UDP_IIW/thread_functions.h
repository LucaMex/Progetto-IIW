#include "basic.h"
#include "common.h"
#include "timer_functions.h"
#include "packet_functions.h"
#include "window_operations.h"

void cond_init(pthread_cond_t * cond);
void mutex_init(pthread_mutex_t* mtx);
void mutex_lock(pthread_mutex_t* mtx);
void mutex_unlock(pthread_mutex_t* mtx);
void wait_cond(pthread_cond_t* cond,pthread_mutex_t* mtx);
void send_signal(pthread_cond_t* cond);
void* thread_job(void* arg);
void start_thread(struct thread_data td,struct sockaddr_in servaddr,int sockfd,Window* w);
