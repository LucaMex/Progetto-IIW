#ifndef PACKET_FUNCTIONS_H_
#define PACKET_FUNCTIONS_H_

#include <errno.h>
#include <stdio.h>
#include "configurations.h"
#include "basic.h"
#include "common.h"
#include "data_types.h"
#include "timer_functions.h"
#include "window_operations.h"

int receive_packet(int sockfd,Header* p,struct sockaddr_in* servaddr);
void send_packet(int sock_fd,struct sockaddr_in servaddr,Header* p,int probability);
void send_ack(int sockfd,Header p,struct sockaddr_in servaddr,int probability,int ack_seq);
int receive_cmd_ack(int sockfd,Header* p,struct sockaddr_in* servaddr);
int receive_command(int sockfd,char comm[],Header* r,struct sockaddr* servaddr);
int receive_ack(Window* w,int sockfd,struct sockaddr_in servaddr,Header* recv,int seq,char c,int existing);
int waiting(int sockfd,struct sockaddr_in servaddr,Header p,int expected_ack);
int wait_ack(int sockfd,struct sockaddr_in servaddr,Header p,int end_seq);


#endif
