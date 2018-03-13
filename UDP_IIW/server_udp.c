#include "basic.h"
#include "configurations.h"
#include "data_types.h"
#include "common.h"
#include "thread_functions.h"
#include "timer_functions.h"
#include "packet_functions.h"
#include "window_operations.h"

extern int n_win;
int adaptive;



void sighandler(int sign)
{
	(void)sign;
	int status;
	pid_t pid;

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






void initialize_socket(int* sock_fd,struct sockaddr_in* s)
{
	int sockfd;
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err_exit("errore in socket");

	if (bind(sockfd, (struct sockaddr *)s, sizeof(*s)) < 0)
		err_exit("error in bind");
	*sock_fd = sockfd;
}






int get_memid()
{
	key_t key = ftok(".",'1');
	if(key == -1)
		err_exit("ftok");
	int id = shmget(key,sizeof(Process_data),IPC_CREAT | 0666);
	if(id == -1)
		err_exit("shmget");
	return id;
}



int get_msg_queue()
{
	key_t key = ftok(".",'b');
	if(key == -1)
		err_exit("ftok");
	int qid = msgget(key,IPC_CREAT |  0666);
	if(qid == -1)
		err_exit("msgget");
	return qid;
}



Process_data* get_shared_memory(int mem_id)
{
	Process_data* p = shmat(mem_id,NULL,0);
	if(p == (void*) -1)
		err_exit("shmget  ");
	return p;
}



void listen_request(int sockfd,Header* p,struct sockaddr_in* addr,socklen_t* len)
{
    struct sockaddr_in servaddr = *addr;
    socklen_t l = *len;
    l = sizeof(servaddr);
    printf("listening request\n");

    if((recvfrom(sockfd, p, sizeof(Header), 0, (struct sockaddr *)&servaddr, &l)) < 0)
         err_exit("recvfrom\n");

    *addr = servaddr;
    *len = l;
    return;
}






void send_file_server(char comm[],int sockfd,Header p,struct sockaddr_in servaddr)
{
	int n_ack_received = 0,next_ack,i = 0;
	int fd = -1;
	struct thread_data td;
	int end_seq = 0,win_ind,start_seq = p.n_seq;
	Header recv_h;
	Window* w = NULL;
	struct timespec arrived;

	initialize_window(&w,'s');

	w->win[w->E].n_ack = start_seq;
	send_ack(sockfd,w->win[w->E],servaddr,COMMAND_LOSS,p.n_seq);			/*send ack for command*/
	w->win[w->E].flag = 1;
	w->E = (w->E + 1)%(n_win + 1);
	int tmp = start_seq;


	if(!receive_ack(w,sockfd,servaddr,&recv_h,p.n_seq,'s',0)){
		close_file(fd);
		printf("not responding client; cannot start operation\n");
		return;
	}

	w->win[w->S].flag = 2;
	increase_window(w);


	int existing = 1;
	if(strncmp(comm,"get",3)== 0){							/*get file*/
		if(!existing_file(comm+4,"./serverDir/")){
			printf("not existing file; try another command\n");
			existing = 0;
		}
		else{
			char* new_path = get_file_path(comm+4,"./serverDir/");
			fd = open_file(new_path,O_RDONLY);
			if(locked_file(fd))
				existing = 0;
		}

	}


	else{
		char* new_path = get_file_path("list_file.txt","./serverDir/");					/*get file on server directory*/
		fd = open_file(new_path,O_RDONLY);
		if(file_lock(fd,LOCK_SH) == -1)
				err_exit("file lock");
	}

	off_t len = 0;
	if(existing){
		len = get_file_len(fd);
		sprintf(p.data,"%zu",len);
	}
	else
		p.data[0] = '\0';					/*if file is locked or not exists, server send data with '\0'
											in first position*/

	insert_in_window(w,p.data,tmp + 1,strlen(p.data));
	send_packet(sockfd,servaddr,&(w->win[w->E]),COMMAND_LOSS);			/*send size file*/
	w->E = (w->E + 1)%(n_win + 1);


	if(!receive_ack(w,sockfd,servaddr,&recv_h,tmp + 1,'s',0)){
		close_file(fd);
		printf("not responding client; cannot start operation\n");
		return;
	}

	if(p.data[0] == '\0')
		return;

	w->win[w->S].flag = 2;
	increase_window(w);

	int tot_read = 0,n_packets = get_n_packets(len);


	for(i = 0; i < n_win; i++){
		read_and_insert(w,len,&tot_read,fd,start_seq + i + 2);
		end_seq = w->win[w->E].n_seq + 1;
		send_packet(sockfd,servaddr,&(w->win[w->E]),PACKET_LOSS);

		w->E = (w->E + 1)%(n_win+1);
		if(tot_read >= len)
			break;
	}

	start_thread(td,servaddr,sockfd,w);				//start thread for retransmitting expired packets*/
	next_ack = w->win[w->S].n_seq;

	for(;;){

		if(n_ack_received == n_packets)
			break;

		if(!receive_packet(sockfd,&p,&servaddr)){		//if no packets arrived, request terminates with error
			close_file(fd);			
			if(tot_read == len)
				printf("sended all file, but received no response from client; operation could not be completed\n");
			else
				printf("not responding client; exiting\n");
			return;
		}

		win_ind = (p.n_ack-start_seq)%(n_win + 1);

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

		if(tot_read>=len)
			continue;

		int nE = (w->E + 1)%(n_win + 1);

		while(nE != w->S){											/*window not full*/
			read_and_insert(w,len,&tot_read,fd,start_seq + i + 2);
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
		}
	}

	w->end = 1;


	/*
	 * after received all ack, server sends message to close connection and wait for client ack;
	 * if no data received for at most 10 times, request terminates
	 */

	close_file(fd);

	if(!wait_ack(sockfd,servaddr,p,end_seq)){
		printf("sended file, but received no response from client\n");
		return;
	}


	free(w->win);
	free(w);


	printf("complete\n");

}







void get_file_server(char comm[],int sockfd,Header p,struct sockaddr_in servaddr)
{
	int fd, i=0, n_pkt,start_seq,win_ind,expected_ack;
	int existing = 0;

	start_seq = p.n_seq;
	fd = create_file(comm+4,"./serverDir/");		//tries creating file; if already exists, return -1
	if(fd == -1)
		printf("file already in server directory");


	/*server get a write lock for file; in this way, other client cannot get file before end
	 * write
	 */

	Window* w = NULL;

	initialize_window(&w,'r');					//set every position of window as free
	win_ind = (p.n_seq - start_seq)%(n_win);
	set_buffered(w,win_ind,p.n_seq);			//save command in window

	increase_receive_win(w);


	if(fd == -1){
		existing = 1;
		set_existing(&p);				//if file exists, server writes character '.' on data
	}

	send_ack(sockfd,p,servaddr,COMMAND_LOSS,p.n_seq);							/*send ack for command*/

	Header recv;

	/*server waiting for size file; if not received, tries 10 times to resend it*/
	if(!receive_ack(w,sockfd,servaddr,&recv,p.n_seq + 1,'r',existing)){
		printf("not responding client; cannot start operation\n");
		return;
	}


	if(recv.data[0] == '.'){			//if file exists, client resend packet with '.' in data
		printf("ending\n");
	    return;
	}

	win_ind = (recv.n_seq - start_seq)%(n_win);
	set_buffered(w,win_ind,recv.n_seq);
	increase_receive_win(w);
	off_t len = conv_in_off_t(recv.data);

	send_ack(sockfd,recv,servaddr,COMMAND_LOSS,recv.n_seq);
	expected_ack = recv.n_seq + 1;

	int tot_read = 0,tot_write = 0;

	for(;;){

		if(tot_write == len)						/*received all packets*/
			break;

		if(!receive_packet(sockfd, &p,&servaddr)){
			printf("not available client;returning..\n");
			return;
		}

		n_pkt = (p.n_seq - start_seq) - 2;
		win_ind = (p.n_seq-start_seq)%n_win;


		/*expected ack is first packet sequence that receiver expects to receive*/

		if(p.n_seq<expected_ack || w->win[win_ind].flag == 0){
			send_ack(sockfd,p,servaddr,COMMAND_LOSS,p.n_seq);			/*lost ack; resend*/
			continue;
		}

		buffering_packet(w,win_ind,len,MAXLINE*n_pkt,p,&tot_read);

		while(w->win[w->S].flag == 0){
			save_packet(w,fd,len,&tot_write);
			++expected_ack;
			increase_receive_win(w);
		}

		send_ack(sockfd,p,servaddr,PACKET_LOSS,p.n_seq);		/*send ack for packet*/

		++i;
	}

	waiting(sockfd,servaddr,p,expected_ack);


	if(close(fd) == -1)				 /*lock on file is automatically released with close*/
		err_exit("close file");

	free(w->win);
	free(w);
}



/***************************************************************
*Process tries receiving command 10 times; if no data 	       *
*arrived, return. Process compare received command, and execute*
*corresponding operation									   *
****************************************************************/
void manage_client(int sockfd,struct msgbuf msg)
{
	Header r;
	struct sockaddr_in servaddr;

    char comm[15];

    int attempts = 0,res;
    for(;;){
    	res = receive_command(sockfd,comm,&r,(struct sockaddr *)&servaddr);
    	if(res == 1)
    		break;
    	else{
    		++attempts;
    		if(attempts == 10){
    			printf("not responding client; exiting\n");
    			return;
    		}
    	}
    }


	if(strncmp(comm, "put", 3) == 0){
		 get_file_server(comm,sockfd,r,msg.s);
	}

	else if(  (strncmp(comm,"list",4) == 0) || (strncmp(comm,"get",3) == 0)  ){
		send_file_server(comm,sockfd,r,msg.s);
	}

	printf("end request\n");

}




void get_semaphore(sem_t* sem)
{
	if(sem_wait(sem) == -1)
		err_exit("sem init");
}


void release_semaphore(sem_t* sem)
{
	if(sem_post(sem) == -1)
		err_exit("sem post");
}



/*************************************************************************************************
 * Child process waits on message queue; when father writes on queue, child executes		     *
 * client request. A child process executes 5 request, then terminates.							 *
 *************************************************************************************************/


void child_job(int qid,int sid,pid_t pid)
{

	int n_req = 0;
	(void)pid;
	Header p;
	struct sockaddr_in addr;
	int sockfd;
	struct msgbuf msg;

	msg.mtype = 1;
	Process_data* pr = get_shared_memory(sid);

	while(n_req < 5){

		if(msgrcv(qid,&msg,	sizeof(struct sockaddr_in) + sizeof(int),1,0) == -1)
			err_exit("msgrcv");

		get_semaphore(&pr->sem);
		--(pr->n_avail);
		release_semaphore(&pr->sem);


		memset((void *)&addr,0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(0);

		initialize_socket(&sockfd,&addr);				//every child process creates a new socket


		p.n_seq = -1;
		p.n_ack = msg.client_seq;

		if (sendto(sockfd, &p, sizeof(Header), 0, (struct sockaddr *)&msg.s, sizeof(msg.s)) < 0)			/*connection ack*/
			err_exit("sendto");

		manage_client(sockfd,msg);						//execute client request
		++n_req;

		get_semaphore(&pr->sem);
		++(pr->n_avail);
		release_semaphore(&pr->sem);
	}

	get_semaphore(&pr->sem);
	--(pr->n_avail);
	release_semaphore(&pr->sem);

	exit(EXIT_SUCCESS);
}





void initialize_processes(int qid,int sid)
{

	(void) sid;
	pid_t pid;
	int i;
	for(i = 0; i < 10; i++){
		pid = fork();
		if(pid == -1)
			err_exit("fork");
		if(pid == 0) child_job(qid,sid,getpid());	   //passing queue id and shared memory id to processes
	}
	return;
}





void write_on_queue(int qid,struct sockaddr_in s,Header p)
{

	 struct msgbuf msg;
	 msg.s = s;
	 msg.client_seq = p.n_seq;
	 size_t size = sizeof(struct sockaddr_in) + sizeof(int);
	 msg.mtype = 1;


	if(msgsnd(qid,&msg,size,0) == -1)
		perror("msgsnd");
}





void create_new_processes(int qid, int sid)
{
	int i;
	pid_t pid;
	for(i = 0; i<5; i++){
		pid = fork();
		if(pid == -1)
			err_exit("fork");
		if(pid == 0){
			Process_data* pr = get_shared_memory(sid);
			get_semaphore(&pr->sem);
			++(pr->n_avail);
			release_semaphore(&pr->sem);
			child_job(qid,sid,getpid());
		}
	}
}


void update_list_file()
{
	struct dirent** namelist;
	int n=scandir("serverDir",&namelist,0,alphasort);
	if(n == -1)
		perror("scandir");

	int fd = open("./serverDir/list_file.txt", O_RDWR | O_CREAT,0666);

	if(file_lock(fd,LOCK_EX) == -1)
		err_exit("locking file");


	int tot = 0;
	int v;

	/*for starting by 2 because one and second position are occupied by '.' and '..', which
	 * indicate current and previous directory */


	for(v = 2; v <n; v+=2){
		tot = tot + strlen(namelist[v]->d_name);
	}
	int x;
	char p = '\n';

	for(x = 2; x < n; x++){
		if(strncmp(namelist[x]->d_name,"list_file.txt",13) == 0)
			continue;
		write_on_file(namelist[x]->d_name,fd,strlen(namelist[x]->d_name));
		if(write(fd, &p,sizeof(char)) == -1)
		   perror("write");

	}
	close_file(fd);					/*lock is automatically released*/
}



int main(int argc, char **argv)
{
  (void) argc;
  (void) argv;
  int sockfd;
  int sid;
  socklen_t len;
  struct sockaddr_in addr;
  Header p;
  struct sigaction sa;


  handle_sigchild(&sa);					/*handle SIGHCLD to avoid zombie processes*/

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
  srand(time(NULL));

  /*
   * Create a message queue where send client data to child processes, in particular
   *  IP and port number memorized in struct sockaddr_in by recvfrom if argument is
   *  not NULL
   */
  int qid = get_msg_queue();


  /*
   * Create shared memory, where set variable with number of available processes, and a semaphore
   */
  sid = get_memid();
  Process_data* pr = get_shared_memory(sid);


  if(sem_init(&pr->sem,1,1) == -1)
	  err_exit("sem init");
  pr->n_avail = 10;						//initial number of processes

  initialize_processes(qid,sid);		//create processes and passing memory id and queue id

  initialize_addr(&addr);


  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)		//create listen socket
      err_exit("errore in socket");

   if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
     err_exit("error in bind");
   }

  for(;;){
	  listen_request(sockfd,&p,&addr,&len);
	  update_list_file();
	  write_on_queue(qid,addr,p);					//write on queue message client data

	  /*
	   * When number of available processes is less than 5, create other 5 processes
	   */

	  if(pr->n_avail <5)
		create_new_processes(qid,sid);
  }

  wait(NULL);
  return 0;
}
