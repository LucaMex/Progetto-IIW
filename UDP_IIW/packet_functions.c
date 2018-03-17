#include <errno.h>
#include <stdio.h>
#include "configurations.h"
#include "basic.h"
#include "common.h"
#include "data_types.h"
#include "timer_functions.h"
#include "window_operations.h"



int receive_packet(int sockfd,Pkt_head* p,struct sockaddr_in* servaddr)
{
	struct sockaddr_in s = *servaddr;
	socklen_t len = sizeof(s);
	int n,attempts = 0;
	for(;;){
		start_socket_timeout(&sockfd,0);
		n = recvfrom(sockfd,p,sizeof(Pkt_head),0,(struct sockaddr*)servaddr,&len);
		if(n == -1){
			if(errno == EWOULDBLOCK){
				if(attempts == 10){
					reset_socket_timeout(&sockfd);
					return 0;
				}
				++attempts;
				continue;
			}
			else
				err_exit("recvfrom");
		}
		else
			break;
	}
	reset_socket_timeout(&sockfd);

	return 1;
}


void send_packet(int sock_fd,struct sockaddr_in servaddr,Pkt_head* p,int probability)
{
	long x = rand()%100 + 1;

	if(x>probability){
		if(sendto(sock_fd,p,sizeof(Pkt_head), 0, (struct sockaddr *)&servaddr,sizeof(servaddr)) < 0)
			err_exit("sendto\n");
	}
}





void send_ack(int sockfd,Pkt_head p,struct sockaddr_in servaddr,int probability,int ack_seq)
{
	p.n_seq = -1;
	p.n_ack = ack_seq;
	long x = rand()%100 + 1;
	if(x > probability){
		if(sendto(sockfd,&p,sizeof(Pkt_head),0,(struct sockaddr *)&servaddr,sizeof(servaddr)) < 0)
			err_exit("sendto\n");
	}
}


int receive_cmd_ack(int sockfd,Pkt_head* p,struct sockaddr_in* servaddr)
{
	struct sockaddr_in s = *servaddr;
	socklen_t len = sizeof(s);
	int n,result = 0;
	start_socket_timeout(&sockfd,0);
	//vedo se ho ricevuto l'ack che attendevo-->fin
	n = recvfrom(sockfd,p,sizeof(Pkt_head),0,(struct sockaddr*)servaddr,&len);
	if(n == -1){
		if(errno == EWOULDBLOCK){
			result = 0;
		}
		else
			err_exit("recvfrom");
	}
	else
		result = 1;
	//resetto il timeout della socket
	reset_socket_timeout(&sockfd);

	return result;
}


int receive_command(int sockfd,char comm[],Pkt_head* r,struct sockaddr* servaddr)
{
	struct sockaddr s = *servaddr;
	socklen_t len = sizeof(s);
	int j = 0,result = 0;
	//inizializzo il timeout(passo attempts=0), e setto la socket in timeout
	start_socket_timeout(&sockfd,0);

    int n = recvfrom(sockfd, r, sizeof(Pkt_head), 0, (struct sockaddr *)servaddr, &len);
    if(n == -1){
    	//EWOULDBLOCK ritornato quando uso non blocking-socket e non c'è abbastanza spazio nel kernel data buffer
    	if(errno == EWOULDBLOCK){
    		errno = 0;
    		result = 0;
    	}
        else
        	err_exit("recvfrom");
    }
    else{
    	result = 1;
    	while(r->data[j] != '\0'){
    		comm[j] = r->data[j];
    		++j;
    	}
    	comm[j] = '\0';
    }
    return result;
}


int receive_ack(Window* w,int sockfd,struct sockaddr_in servaddr,Pkt_head* recv,int seq,char c,int existing)
{
	Pkt_head send;
	int attempts = 0;
	for(;;){
		if(attempts == 10)
			return 0;
		printf("\rwaiting file response..");
		receive_cmd_ack(sockfd,recv,&servaddr);
		if(c == 's'){
			if(recv->n_ack != seq){
				check_window(w,w->E,sockfd,servaddr);
				++attempts;
			}
			else
				break;
		}
		else{
			if(recv->n_seq != seq){
				if(existing)
					set_existing(&send);
				send_ack(sockfd,send,servaddr,COMMAND_LOSS,seq-1);							/*send ack for command*/
				++attempts;
			}
			else
				break;
		}

	}
	return 1;
}



int waiting(int sockfd,struct sockaddr_in servaddr,Pkt_head p,int expected_ack)
{
	int attempts = 0;
	for(;;){
		receive_cmd_ack(sockfd,&p,&servaddr);			/*receiving fin ack*/

		if(p.n_seq<expected_ack){
			send_ack(sockfd,p,servaddr,COMMAND_LOSS,p.n_seq);
			++attempts;
			if(attempts == 10)
				break;
		}
		else{
			send_ack(sockfd,p,servaddr,COMMAND_LOSS,p.n_seq);
			return 1;
		}
	}
	return 1;
}



int wait_ack(int sockfd,struct sockaddr_in servaddr,Pkt_head p,int end_seq)
{
	p.data[0] = '\0';
	p.n_seq = end_seq;
	send_packet(sockfd,servaddr,&p,COMMAND_LOSS);

	for(;;){
		if(!receive_packet(sockfd,&p,&servaddr))
			return 0;
		if(p.n_ack == end_seq)
			break;
	}

	return 1;
}
