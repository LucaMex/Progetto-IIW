#include "configurations.h"
#include "basic.h"
#include "data_types.h"
#include "common.h"
#include "thread_functions.h"
#include "timer_functions.h"
#include "packet_functions.h"
#include "window_operations.h"

int n_request = 0;
extern int n_win;
int adaptive;

void sighandler(int sign)
{
	(void)sign;
	int status;
	pid_t pid;

	--n_request;


	while ((pid = waitpid(WAIT_ANY, &status, WNOHANG)) > 0)
		;
	return;
}




void handle_sigchild(struct sigaction* sa)
{

    sa->sa_handler = sighandler;
    sa->sa_flags = SA_RESTART;
    sigemptyset(&sa->sa_mask);

    if (sigaction(SIGCHLD, sa, NULL) == -1) {
        fprintf(stderr, "Error in sigaction()\n");
        exit(EXIT_FAILURE);
    }
}





int request_to_server(int sockfd,Header* x,struct sockaddr_in* addr)
{
    int n;
    struct sockaddr_in s = *addr;
    struct timespec conn_time = {2,0};
    int attempts = 0;
    socklen_t len = sizeof(s);
    x->n_seq = generate_casual();


    /******************************************************************
     * Process tries to connect to server and generates a random int; *
     * timeout on recvfrom is setted; if no response received,        *
     * timeout is increased; if no response for 3 times, process      *
     * returns.                                                       *
     ******************************************************************/


    for(;;){
    	if(sendto(sockfd, x,sizeof(Header), 0, (struct sockaddr *)&s,sizeof(s)) < 0){
    		printf("errore\n");
    		err_exit("sendto\n");
    	}

    	if(setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&conn_time,sizeof(conn_time)) < 0)
    		err_exit("setsockopt failed\n");
    	n = recvfrom(sockfd,x,sizeof(Header),0,(struct sockaddr*)&s,&len);

    	if(n < 0){
    		if(errno == EWOULDBLOCK){
    			printf("server not responding; trying again..\n");
    			++attempts;
    			if(attempts == 3)
    				break;
    			conn_time.tv_sec += conn_time.tv_sec;
    		}
    		else
    			err_exit("recvfrom");
    	}
    	if(n>0){
    		conn_time.tv_sec = 0;
    		conn_time.tv_nsec = 0;
        	if(setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&conn_time,sizeof(conn_time)) < 0)
        		err_exit("setsockopt failed\n");
    		printf("client connected!\n");
    	    *addr = s;
    	    return 1;
    	}
    }

    return 0;					/*not available server*/
}






void print_list(Window* w, off_t len,int*tot_write)
{
	int n_bytes = get_n_bytes(len,*tot_write);
	printf("\n%s",w->win[w->S].data);
	*tot_write = *tot_write + n_bytes;

}




void get_file_client(int sockfd,char* line, Header p, struct sockaddr_in servaddr)
{
	int fd = -1, win_ind, i=0, first_seq=p.n_ack;
	Header h_recv,h_send;
	off_t len;

	if(strncmp(line,"get",3) == 0){
		fd = create_file(line+4,"./clientDir/");
		if(fd == -1){
			printf("file already exists in your directory\n");
			return;
		}
	}

	Window* w = NULL;
	initialize_window(&w,'r');

	i = 1;
	++first_seq;

	insert_in_window(w,line,first_seq,strlen(line));
	h_send = w->win[w->E];
	send_packet(sockfd,servaddr,&(w->win[w->E]),COMMAND_LOSS);
	w->E = (w->E + 1)%(n_win);

	++i;


	if(!receive_ack(w,sockfd,servaddr,&h_recv,first_seq,'s',0)){
		printf("Server not responding; cannot start operation\n");
		return;
	}

	int tmp = h_recv.n_ack;
	increase_receive_win(w);
	send_ack(sockfd,h_send,servaddr,COMMAND_LOSS,tmp);			/*send ack*/


	if(!receive_ack(w,sockfd,servaddr,&h_recv,tmp+1,'r',0)){
		printf("Cannot receive size file from server; exiting\n");
		return;
	}


	w->win[w->S] = h_recv;
	increase_receive_win(w);
	if(strncmp(line,"get",3) == 0)
		printf("   size file = %s byte\n",h_recv.data);

	send_ack(sockfd,h_send,servaddr,COMMAND_LOSS,h_send.n_ack+1);			/*send len ack*/

	if(h_recv.data[0] == '\0'){
		printf("not existing file in server directory\n");
		return;
	}
	len = conv_in_off_t(h_recv.data);

	int expected_ack = h_recv.n_seq + 1;
	int tot_read = 0,tot_write = 0;
	int n_pkt;

	if(strncmp(line,"list",4) == 0)
		printf("\n\n\n");				/*only to clearly show files to user*/

	for(;;){
		if(strncmp(line,"get",3) == 0)
			printf("\rexecuting download");

		if(tot_write == len)						/*received all packets*/
			break;

		if(!receive_packet(sockfd, &p,&servaddr)){
			return;
		}

		n_pkt = (p.n_seq - first_seq -2);
		win_ind = (p.n_seq-first_seq)%n_win;
		w->win[win_ind].n_seq = p.n_seq;


		if(p.n_seq<expected_ack || w->win[win_ind].flag == 0){

			send_ack(sockfd,p,servaddr,PACKET_LOSS,p.n_seq);			/*lost ack; resend*/
			continue;
		}

		buffering_packet(w,win_ind,len,MAXLINE*n_pkt,p,&tot_read);

		while(w->win[w->S].flag == 0){
			if(strncmp(line,"list",4) == 0)
				print_list(w,len,&tot_write);

			else
				save_packet(w,fd,len,&tot_write);

			++expected_ack;
			increase_receive_win(w);
		}
		send_ack(sockfd,p,servaddr,PACKET_LOSS,p.n_seq);		/*send ack for packet*/

		++i;
	}

	if(!waiting(sockfd,servaddr,p,expected_ack))
		printf("Complete operation, but server busy\n");

	else
		printf("Complete!\n");

	if(fd == -1)
		return;

	if(close(fd) == -1)
		err_exit("close file");

	free(w->win);
	free(w);
}








void send_file_client(int sockfd,char* line, Header p, struct sockaddr_in servaddr)
{
	struct thread_data td;
	int next_ack,n_ack_received = 0,win_ind,end_seq = 0,first_seq=p.n_ack;
	int n_packets,fd,tot_read = 0;						/*to avoid warning*/
	int i = 1;
	char* filename = line + 4;
	struct timespec arrived;
	off_t len = 0;
	Header recv;

	Window* w = NULL;
	initialize_window(&w,'s');

	insert_in_window(w,line,first_seq+i,strlen(line));
	send_packet(sockfd,servaddr,&(w->win[w->E]),COMMAND_LOSS);
	w->E = (w->E + 1)%(n_win + 1);


	if(!receive_ack(w,sockfd,servaddr,&recv,first_seq+i,'s',0)){
		printf("not responding server; cannot start operation\n");
		return;
	}

	w->win[w->S].flag = 2;
	increase_window(w);

	if(recv.data[0] == '.'){
		printf("File already exists in server directory\n");
		p.data[0] = '.';
	}


	else
		write_file_len(&len,&fd,&p,filename,"./clientDir/");

	++i;

	insert_in_window(w,p.data,first_seq + i,strlen(p.data));
	send_packet(sockfd,servaddr,&(w->win[w->E]),COMMAND_LOSS);		/*send file len*/
	w->E = (w->E + 1)%(n_win + 1);


	if(recv.data[0] == '.')					/*not existing file*/
		return;


	if(!receive_ack(w,sockfd,servaddr,&recv,first_seq + i,'s',0)){
		printf("not responding server; end operation\n");
		return;
	}


	w->win[w->S].flag = 2;
	increase_window(w);

	n_packets = get_n_packets(len);
	for(i = 0; i < n_win; i++){
		read_and_insert(w,len,&tot_read,fd,first_seq + i + 3);
		send_packet(sockfd,servaddr,&(w->win[w->E]),PACKET_LOSS);
		end_seq = w->win[w->E].n_seq + 1;
		w->E = (w->E + 1)%(n_win+1);
		if(tot_read >= len)
			break;
	}


	start_thread(td,servaddr,sockfd,w);

	next_ack = w->win[w->S].n_seq;

	printf("sending file...\n");


	for(;;){
		if(n_ack_received == n_packets)
			break;

		if(!receive_packet(sockfd, &p,&servaddr)){
			if(tot_read == len)
				printf("sended all file but received no response; operation could not be completed\n");

			else
				printf("received no response; cannot complete operation..\n");
			return;
		}

		win_ind = (p.n_ack-first_seq - 1)%(n_win + 1);


		if(p.n_ack<next_ack || (w->win[win_ind].flag == 2))
			continue;

		mutex_lock(&w->mtx);
		w->win[win_ind].flag = 2;				/*received ack*/
		mutex_unlock(&w->mtx);


		if(adaptive == 1){
			start_timer(&arrived);
			calculate_timeout(arrived,w->win[win_ind].tstart);
		}

		next_ack = next_ack + increase_window(w);
		++n_ack_received;

		if(tot_read>=len){

			continue;
		}

		int nE = (w->E + 1)%(n_win + 1);


		while(nE != w->S){							/*window not full*/
			read_and_insert(w,len,&tot_read,fd,first_seq + i + 3);
			send_packet(sockfd,servaddr,&(w->win[w->E]),PACKET_LOSS);

			if(tot_read == len){
				end_seq = w->win[w->E].n_seq + 1;
			}
			w->E = nE;
			nE = (nE + 1)%(n_win + 1);

			++i;

			if(tot_read == len){
				break;
			}
		}			/*end while*/
	}		/*end for*/

	p.n_seq = end_seq;
	w->end = 1;


	if(!wait_ack(sockfd,servaddr,p,end_seq)){
		printf("Complete operation, but server busy\n");
		return;
	}

	free(w->win);
	free(w);

	printf("Complete operation!\n");

}








int main(int argc, char *argv[]) {

  int   sockfd;
  struct    sockaddr_in   servaddr;
  Header p;
  struct sigaction sa;


  if(DIMWIN <= 0)
	  n_win = 80;
  if(DIMWIN > 93)
	  n_win = 93;
  else
	  n_win = DIMWIN;


  if(ADAPTATIVE != 1)
	  adaptive = 0;
  else
	  adaptive = 1;

  handle_sigchild(&sa);


  if (argc < 2) {
    fprintf(stderr, "utilizzo: daytime_clientUDP <indirizzo IP  server>\n");
    exit(1);
  }


  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* crea il socket   */
	  err_exit("socket");
   }

   memset((void *)&servaddr, 0, sizeof(servaddr));
   servaddr.sin_family = AF_INET;
   servaddr.sin_port = htons(SERVPORT);

   if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) {
        err_exit("error in inet_pton for %s");
   }


  pid_t pid;
  char* line;

  /*************************************************************
   * father process waits command from standard input; then,   *
   * creates a new process to execute. If CTRL + D received,   *
   * waits all children; then, father terminates.               *
   *************************************************************/



  while(feof(stdin) == 0){
	  //ssize_t len_line;
	  char comm[5];


	  printf("write command\n");
	  line = read_from_stdin();


	  if(line == NULL){
		  wait(NULL);
		  printf("terminated all request; closing connection\n");
		  break;
	  }

	  ++n_request;
	  if(n_request > 5){
		  printf("too many request; wait\n");
		  continue;
	  }

	  pid = fork();

	  if(pid != 0)
		  continue;

	  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* create new socket   */
		   err_exit("socket");
	  }

	  if(!request_to_server(sockfd,&p,&servaddr)){
		  printf("not available server\n");
		  exit(EXIT_SUCCESS);
	  }

	  strncpy(comm, line, 4);
	  comm[strlen(comm)] = '\0';

	  if(strncmp(comm, "put", 3) == 0){
		  if(!existing_file(line+4,"./clientDir/")){
			  printf("not existing file\n");
			  break;
		  }
		  send_file_client(sockfd,line,p,servaddr);
		  break;
	  }

	  else if((strncmp(comm,"get",3) == 0) || (strncmp(comm,"list",4) == 0)){
		  get_file_client(sockfd,line,p,servaddr);
		  break;
	  }


	  else{
	  		fprintf(stderr, "command not recognize. USE COMMAND  LIST or GET/PUT FOLLOWEWD BY A PROPER FILE NAME \n");
	  		break;
	  }


      }
      printf("closing connection\n");

      exit(EXIT_SUCCESS);
}
