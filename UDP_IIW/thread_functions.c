#include "basic.h"
#include "common.h"
#include "timer_functions.h"
#include "packet_functions.h"
#include "window_operations.h"

void cond_init(pthread_cond_t * cond)
{
	if(pthread_cond_init(cond,NULL) != 0)
		err_exit("cond init\n");
}


void mutex_init(pthread_mutex_t* mtx)
{
	if(pthread_mutex_init(mtx,NULL) != 0)
		err_exit("mutex init");
}


void mutex_lock(pthread_mutex_t* mtx)
{
	if(pthread_mutex_lock(mtx) != 0)
		err_exit("mutex lock");
}


void mutex_unlock(pthread_mutex_t* mtx)
{
	if(pthread_mutex_unlock(mtx) != 0)
		err_exit("mutex unlock");


}

void wait_cond(pthread_cond_t* cond,pthread_mutex_t* mtx)
{
	if(pthread_cond_wait(cond,mtx)!= 0)
		err_exit("cond wait");


}


void send_signal(pthread_cond_t* cond)
{
	if(pthread_cond_signal(cond) != 0)
		err_exit("cond wait");

}



void* thread_job(void* arg)
{
	struct thread_data* td = (struct thread_data*) arg;
	Window* w;
	int sockfd;
	struct sockaddr_in servaddr;
	w = td->w;
	sockfd = td->sockfd;

	servaddr = td->servaddr;

	struct timespec time_check = {0,7000000};

	for(;;){
		nanosleep(&time_check,NULL);
		check_window(w,w->E,sockfd,servaddr);				/*control expired packets*/
		if(w->end == 1)
			break;
	}
	return NULL;
}





void start_thread(struct thread_data td,struct sockaddr_in servaddr,int sockfd,Window* w)
{
	td.servaddr = servaddr;
	td.sockfd = sockfd;
	td.w = w;
	if(pthread_create(&(td.tid),NULL,thread_job,&td) != 0)
		err_exit("pthread create");
}




