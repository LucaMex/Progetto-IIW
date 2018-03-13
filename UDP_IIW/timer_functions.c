#include "common.h"
#include "data_types.h"

double alpha = 0.125,beta=0.25;
long estimated = 0,deviance = 0;
struct timespec start_timeout = {0,15000000};

void start_socket_timeout(int* sockfd,int attempts)
{
	struct timespec conn_time = {0,0};
	if(attempts == 0){
		conn_time.tv_sec = 0;
		conn_time.tv_nsec = 950000;
	}
	else
		conn_time.tv_sec = 5*attempts;
	int sock = *sockfd;
	if(setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&conn_time,sizeof(conn_time)) < 0)
	    err_exit("setsockopt failed\n");
}


void reset_socket_timeout(int* sockfd)
{
	struct timespec conn_time = {0,0};
	int sock = *sockfd;
	if(setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&conn_time,sizeof(conn_time)) < 0)
	    err_exit("setsockopt failed\n");
}



void start_timer(struct timespec* time)
{
	time->tv_sec = 0;
	time->tv_nsec = 0;


	if(clock_gettime(CLOCK_MONOTONIC_RAW,time) == -1)
		err_exit("clock gettime\n");
}



void calculate_timeout(struct timespec end_time,struct timespec packet_time)
{
	long old_estimated = estimated;
	double sec = difftime(end_time.tv_sec,packet_time.tv_sec);
	long nsec = end_time.tv_nsec - packet_time.tv_nsec;
	long sec_nano = sec*(1.0e9);

	long time_tot = sec_nano + nsec;

	if(time_tot >(3*(1.0e6)))
		return;
	if(time_tot<(1*(1.0e6)))
		return;
	estimated = ((1-alpha)*(old_estimated)) + ((alpha)*(time_tot));
	deviance = ((1-beta)*(deviance)) + (beta*(labs(time_tot - estimated)));
	time_tot = estimated + (4*deviance);
	start_timeout.tv_sec = time_tot/(1.0e9);
	if(start_timeout.tv_sec > 0)
		start_timeout.tv_nsec = time_tot - (start_timeout.tv_sec*(1.0e9));
	else
		start_timeout.tv_nsec = time_tot;

}







void set_timeout(struct timespec* time_pkt)
{
	time_pkt->tv_sec = start_timeout.tv_sec;
	time_pkt->tv_nsec = start_timeout.tv_nsec;
}




int expired_timeout(struct timespec actual,struct timespec packet_time)
{
	long time1 = actual.tv_sec*(1.0e9) + actual.tv_nsec;
	long time2 = packet_time.tv_sec*(1.0e9) + packet_time.tv_nsec;
	long elapsed = time1 - time2;
	long time3 = start_timeout.tv_sec*(1.0e9) + start_timeout.tv_nsec;

	if(elapsed > time3)
		return 1;

	return 0;
}
