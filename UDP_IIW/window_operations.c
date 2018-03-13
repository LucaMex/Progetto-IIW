#include "configurations.h"
#include "basic.h"
#include "common.h"
#include "timer_functions.h"
#include "packet_functions.h"


extern int n_win;

void initialize_window(Window** w,char c)
{
	Window* window;
	int flag;
	if(c == 's')
		flag = -2;
	else
		flag = 1;
	window = malloc(sizeof(Window));
	if(window == NULL)
		err_exit("malloc");
	window->win = malloc((n_win+1)*sizeof(Header));
	if(window->win == NULL)
		err_exit("malloc");
	window->E = window->S = window->end = 0;
	int j;
	for(j = 0; j < n_win+1; j++)
		window->win[j].flag= flag;
	*w = window;
}


int increase_window(Window* w)
{
	int x = 0;
	while(w->win[w->S].flag == 2){
		++x;
		w->win[w->S].flag = -2;
		w->S = (w->S + 1)%(n_win+1);
	}
	return x;
}




/***********************************************************************************
 * setted flag to 1; it indicates buffer position as free and increase 			   *
 * index for receiving new data													   *
 ***********************************************************************************/
void increase_receive_win(Window* w)
{
	w->win[w->S].flag = 1;
	w->S= (w->S + 1)%n_win;

}




void check_window(Window* w,int n_ack,int sockfd,struct sockaddr_in servaddr)
{
	int j;

	struct timespec time_actual;
	start_timer(&time_actual);


	/***************************************************
	 * if sended packets, its flag is 1; when a packet *
	 * is sended, if timer is expired packet is sended *
	 * again										   *
	 ***************************************************/


	for(j = w->S; j!= n_ack;){
		if(w->win[j].flag == 1){

			if(expired_timeout(time_actual,w->win[j].tstart)){
				start_timer(&(w->win[j].tstart));					/*restart timer*/
				send_packet(sockfd,servaddr,&(w->win[j]),COMMAND_LOSS);			/*resend packet*/
			}
		}
		j = (j+1)%(n_win + 1);
	}

}


void insert_in_window(Window*w, char data[],int first_seq,int n_bytes)
{
	w->win[w->E].n_seq = first_seq;
	copy_data(w->win[w->E].data,data,n_bytes);
	set_timeout(&(w->win[w->E].tstart));
	start_timer(&(w->win[w->E].tstart));
	w->win[w->E].flag = 1;

}





void read_and_insert(Window* w,off_t len,int* tot_read,int fd,int seq)
{
	char* buffer = malloc(MAXLINE*sizeof(char));
	if(buffer == NULL)
		err_exit("malloc");
	int n_bytes = get_n_bytes(len,*tot_read);
	int r = read_file(buffer,fd,n_bytes);
	*tot_read = *tot_read + r;
	insert_in_window(w,buffer,seq,n_bytes);
	free(buffer);
}



/********************************************************************************************
 * get size to read from file and copy data in window; then, packet flag is setted to 0, to *
 * indicates packet is buffered.															*
 ********************************************************************************************/
void buffering_packet(Window* w,int win_ind,off_t len,int size,Header p,int* tot_read)
{
	int n_bytes = get_n_bytes(len,size);
	copy_data(w->win[win_ind].data,p.data,n_bytes);
	*tot_read = *tot_read + n_bytes;
	w->win[win_ind].flag = 0;
	w->win[win_ind].n_seq = p.n_seq;

}


void save_packet(Window* w,int fd,off_t len, int* tot_write)
{
	int n_bytes = get_n_bytes(len,*tot_write);
	write_on_file(w->win[w->S].data,fd,n_bytes);
	*tot_write = *tot_write + n_bytes;
}



void set_existing(Header* p)
{
	int x;
	for(x = 0; x < MAXLINE; x++)
		p->data[x]='.';
}




void set_buffered(Window* w,int win_ind,int n_seq)
{
	w->win[win_ind].flag = 0;
	w->win[win_ind].n_seq = n_seq;

}
