#include <time.h>
#include <math.h>
#include <stdlib.h>
#include "common.h"


void start_socket_timeout(int* sockfd,int attempts);
void reset_socket_timeout(int* sockfd);
void start_timer(struct timespec* time);
void calculate_timeout(struct timespec end_time,struct timespec packet_time);
void set_timeout(struct timespec* time_pkt);
int expired_timeout(struct timespec actual,struct timespec packet_time);
