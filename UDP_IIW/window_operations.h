#ifndef WINDOW_OPERATIONS_H_
#define WINDOW_OPERATIONS_H_

#include "configurations.h"
#include "basic.h"
#include "common.h"
#include "timer_functions.h"
#include "packet_functions.h"

void initialize_window(Window** w,char c);
int increase_window(Window* w);
void increase_receive_win(Window* w);
void check_window(Window* w,int n_ack,int sockfd,struct sockaddr_in servaddr);
void insert_in_window(Window*w, char data[],int first_seq,int n_bytes);
void read_and_insert(Window* w,off_t len,int* tot_read,int fd,int seq);
void buffering_packet(Window* w,int win_ind,off_t len,int size,Header p,int* tot_read);
void save_packet(Window* w,int fd,off_t len, int* tot_write);
void set_existing(Header* p);
void set_buffered(Window* w,int win_ind,int n_seq);

#endif
