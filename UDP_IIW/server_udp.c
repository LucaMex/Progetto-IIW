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
	//Con flag WNOHANG-->il padre ritorna se non ci sono figli morti(non bloccante)
	while ((pid = waitpid(WAIT_ANY, &status, WNOHANG)) > 0)
		;
	return;
}




void handle_sigchild(struct sigaction* sa)
{
	//Riempio la struttura dati con i valori richiesti
		//sighandler puntatore a funzione(==nome funzione)
    sa->sa_handler = sighandler;
    sa->sa_flags = SA_RESTART;
    sigemptyset(&sa->sa_mask);
    //registro il sigaction
    if (sigaction(SIGCHLD, sa, NULL) == -1) {
        fprintf(stderr, "Error in sigaction()\n");
        exit(EXIT_FAILURE);
    }
}






void initialize_socket(int* sock_fd,struct sockaddr_in* s)
{
	int sockfd;
	//creo la socket
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err_exit("errore in socket");
	//associo la socket alla struttura dati precedentemente riempita
	if (bind(sockfd, (struct sockaddr *)s, sizeof(*s)) < 0)
		err_exit("error in bind");
	*sock_fd = sockfd;
}






int get_memid()
{
	key_t key = ftok(".",'1');
	if(key == -1)
		err_exit("ftok");
	int id = shmget(key,sizeof(Manage_request),IPC_CREAT | 0666);
	if(id == -1)
		err_exit("shmget");
	return id;
}



int get_msg_queue()
{
	//creo chiave per coda di messaggi
	key_t key = ftok(".",'b');
	//esco in errore 
	if(key == -1)
		err_exit("ftok");
	//prendo al coda di messaggi 
	int qid = msgget(key,IPC_CREAT |  0666);
	if(qid == -1)
		err_exit("msgget");
	return qid;
}



Manage_request* get_shared_memory(int mem_id)
{
	//mappo la memoria condivisa (presa da mem_id) sulla struct che gestisce il lavoro(Manage_request)
	Manage_request* p = shmat(mem_id,NULL,0);
	if(p == (void*) -1)
		err_exit("shmget  ");
	return p;
}



void listen_request(int sockfd,Pkt_head* p,struct sockaddr_in* addr,socklen_t* len)
{
    struct sockaddr_in servaddr = *addr;
    socklen_t l = *len;
    l = sizeof(servaddr);
    printf("listening request\n");

    if((recvfrom(sockfd, p, sizeof(Pkt_head), 0, (struct sockaddr *)&servaddr, &l)) < 0)
         err_exit("recvfrom\n");

    *addr = servaddr;
    *len = l;
    return;
}






void send_file_server(char comm[],int sockfd,Pkt_head p,struct sockaddr_in servaddr)
{
	int n_ack_received = 0,next_ack,i = 0;
	int fd = -1;
	struct thread_data td;
	int end_seq = 0,win_ind,start_seq = p.n_seq;
	Pkt_head recv_h;
	Window* w = NULL;
	struct timespec arrived;
	//inizializzo la finestra con flag s
	initialize_window(&w,'s');
	//setto con la sequenza inziale 
	w->win[w->E].n_ack = start_seq;

	//invio l'ack del comando
	send_ack(sockfd,w->win[w->E],servaddr,COMMAND_LOSS,p.n_seq);

	w->win[w->E].flag = 1;
	w->E = (w->E + 1)%(n_win + 1);
	int tmp = start_seq;

	// se non ricevo nulla per 10 tentativi(la recevie_ack tenta 10 volte )-->chiudo connessione
	if(!receive_ack(w,sockfd,servaddr,&recv_h,p.n_seq,'s',0)){
		close_file(fd);
		printf("not responding client; cannot start operation\n");
		return;
	}
	//ricevuto-->pkt pieno
	w->win[w->S].flag = 2;
	//sposto finestra 
	increase_window(w);


	int existing = 1;
	//Get file
	if(strncmp(comm,"get",3)== 0){		
		//controllo se il file richiesto esiste-->altrimenti esco				
		if(!existing_file(comm+4,"./serverDir/")){
			printf("not existing file; try another command\n");
			existing = 0;
		}
		//file esiste
		else{
			char* new_path = get_file_path(comm+4,"./serverDir/");
			fd = open_file(new_path,O_RDONLY);
			//prendo il lock sul file
			if(locked_file(fd))
				existing = 0;
		}

	}


	else{
		//prendo la lista -->non era una get quindi è una list
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
		//se il file è gia in   lock--->ho messo existing =0(oppure non esiste)-->server invia '\0' in prima posizione
		p.data[0] = '\0';					

	insert_in_window(w,p.data,tmp + 1,strlen(p.data));
	//invio dim del pkt
	send_packet(sockfd,servaddr,&(w->win[w->E]),COMMAND_LOSS);			
	//sposto E del buffer circolare 
	w->E = (w->E + 1)%(n_win + 1);

	//Se il client non risponde chiudo
	if(!receive_ack(w,sockfd,servaddr,&recv_h,tmp + 1,'s',0)){
		close_file(fd);
		printf("not responding client; cannot start operation\n");
		return;
	}
	//se exisisting =0 -->finisco
	if(p.data[0] == '\0')
		return;
	//metto il flag = 2-->pkt pieno
	w->win[w->S].flag = 2;
	//sposto finestra
	increase_window(w);

	//calcolo quanti pkt sara composto il mesaggio in base alla lunghezzad del file richiesto
	int tot_read = 0,n_packets = get_n_packets(len);

	//per tutta la finestra
	for(i = 0; i < n_win; i++){

		read_and_insert(w,len,&tot_read,fd,start_seq + i + 2);
		//aggiorno end seq inbase alla seq che sono riuscito ad inviare
		end_seq = w->win[w->E].n_seq + 1;
		//invio pkt-->sempre con prob% di perdita
		send_packet(sockfd,servaddr,&(w->win[w->E]),PACKET_LOSS);
		//aggiorno posizioni buffer ciroclare
		w->E = (w->E + 1)%(n_win+1);
		//Se ho letto tutto -->chiudo
		if(tot_read >= len)
			break;
	}

	//faccio partire un thread per la gestione dei pkt da ritrasmettere 
	start_thread(td,servaddr,sockfd,w);
	//ack successivo che mi aspetto				
	next_ack = w->win[w->S].n_seq;

	for(;;){
		//se ho ricevuto tanti pacchetti quanti ne aspettavo finisco
		if(n_ack_received == n_packets)
			break;
		//se non ricevo niente-->chiud con errore
		if(!receive_packet(sockfd,&p,&servaddr)){		
			close_file(fd);			
			if(tot_read == len)
				printf("sended all file, but received no response from client; operation could not be completed\n");
			else
				printf("not responding client; exiting\n");
			return;
		}
		//calcolo nuovo indice
		win_ind = (p.n_ack-start_seq)%(n_win + 1);

		if(p.n_ack<next_ack || (w->win[win_ind].flag == 2))
			continue;
			//prendo il lock
		mutex_lock(&w->mtx);
		//ho ricevuto l'ack-->pkt pieno-->2
		w->win[win_ind].flag = 2;	
		//Rilasci lock
		mutex_unlock(&w->mtx);

		//controllo se il timer è adattativo-->se si lo calcolo
		if(adaptive == 1){
			start_timer(&arrived);
			calculate_timeout(arrived,w->win[win_ind].tstart);
		}
		//aumento finestra di tante posizioni quanti ne ho occupate(flag==2)
		next_ack = next_ack + increase_window(w);
		
		++n_ack_received;

		if(tot_read>=len)
			continue;

		int nE = (w->E + 1)%(n_win + 1);
		//finche la window non è piena 
		while(nE != w->S){						

			read_and_insert(w,len,&tot_read,fd,start_seq + i + 2);
			send_packet(sockfd,servaddr,&(w->win[w->E]),PACKET_LOSS);

			if(tot_read == len){
				end_seq = w->win[w->E].n_seq + 1;
			}
			//aggiorno buffer circloare
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
	//se non ricevo niente esco
	if(!wait_ack(sockfd,servaddr,p,end_seq)){
		printf("sended file, but received no response from client\n");
		return;
	}
	//libero la memoria

	free(w->win);
	free(w);


	printf("complete\n");

}







void get_file_server(char comm[],int sockfd,Pkt_head p,struct sockaddr_in servaddr)
{
	int fd, i=0, n_pkt,start_seq,win_ind,expected_ack;
	int existing = 0;
	//sequenza iniziale uguale a quella del pktHead
	start_seq = p.n_seq;
	//prendo il file descriptor
	fd = create_file(comm+4,"./serverDir/");		
	if(fd == -1)
		printf("file already in server directory");


	/*server get a write lock for file; in this way, other client cannot get file before end
	 * write
	 */

	Window* w = NULL;
	//passo un puntatore doppio e flag r
	initialize_window(&w,'r');						

	//calcolo l'indice rispetto ai valori sequenza e la lughezza della finestra
	win_ind = (p.n_seq - start_seq)%(n_win);
	
	//salvo il comando nella finestra
	set_buffered(w,win_ind,p.n_seq);		
	
	/*
	setto il flag della finestra ad 1 e indico cosi che la posizione è libera
	incremento l'indice di ricezione dati
	*/
	increase_receive_win(w);


	if(fd == -1){
		existing = 1;
		set_existing(&p);				//if file exists, server writes character '.' on data
	}

	send_ack(sockfd,p,servaddr,COMMAND_LOSS,p.n_seq);							/*send ack for command*/

	Pkt_head recv;

	/*server waiting for size file; if not received, tries 10 times to resend it*/
	if(!receive_ack(w,sockfd,servaddr,&recv,p.n_seq + 1,'r',existing)){
		printf("not responding client; cannot start operation\n");
		return;
	}

	//se ho ricevuto '.' vuol dire che il file esisteva, il client se ne è accorto e mi ha mandatp '.'-->chiudo
	if(recv.data[0] == '.'){			
		printf("ending\n");
	    return;
	}

	//calcolo indice con la classica operazione modulo
	win_ind = (recv.n_seq - start_seq)%(n_win);

	set_buffered(w,win_ind,recv.n_seq);
	
	increase_receive_win(w);
	//converto in lunghezza-->lunghezza del file ricevuta
	off_t len = conv_in_off_t(recv.data);
	//invio ack
	send_ack(sockfd,recv,servaddr,COMMAND_LOSS,recv.n_seq);
	//mi aspetto l'ack adesso successivo a quello del n_seq
	expected_ack = recv.n_seq + 1;

	int tot_read = 0,tot_write = 0;
	//inizio la ricezione di tutti i pkt del file
	for(;;){

		if(tot_write == len)						
			break;

		if(!receive_packet(sockfd, &p,&servaddr)){
			printf("not available client;returning..\n");
			return;
		}
		//aggiorno n_pkt e indicie window
		n_pkt = (p.n_seq - start_seq) - 2;
		win_ind = (p.n_seq-start_seq)%n_win;


		// ho perso l'ack -->rinvia 
		if(p.n_seq<expected_ack || w->win[win_ind].flag == 0){
			send_ack(sockfd,p,servaddr,COMMAND_LOSS,p.n_seq);			
			continue;
		}

		buffering_packet(w,win_ind,len,MAXLINE*n_pkt,p,&tot_read);
		//se trovo flag ==0-->salvo il pkt ovvero scrivo sul buffer 
		while(w->win[w->S].flag == 0){
			save_packet(w,fd,len,&tot_write);
			++expected_ack;
			//incremento finestra di riczione
			increase_receive_win(w);
		}
		//invio ack del pkt ricevuto
		send_ack(sockfd,p,servaddr,PACKET_LOSS,p.n_seq);		
		++i;
	}

	//attendo l'ack di fine trasmissione
	waiting(sockfd,servaddr,p,expected_ack);

	//chiudo il file-->tanto il lock viene rilasciato automaticamente
	if(close(fd) == -1)		
		err_exit("close file");
	//libero memoria allocata
	free(w->win);
	free(w);
}



/***************************************************************
Process tries receiving command 10 times; if no data 	       *
*arrived, return. Process compare received command, and execute*
*corresponding operation									   *
****************************************************************/
void manage_client(int sockfd,struct msgbuf msg)
{
	Pkt_head r;
	struct sockaddr_in servaddr;

    char comm[15];

    int attempts = 0,res;
    for(;;){
    	//ricevo il comando del client(1 ho ricevuto ,altrimento niente)
    	res = receive_command(sockfd,comm,&r,(struct sockaddr *)&servaddr);
    	if(res == 1)
    		break;
    	else{
    		//incremento attempts-->fino ad un max di 10 tentativi
    		++attempts;
    		if(attempts == 10){
    			printf("not responding client; exiting\n");
    			return;
    		}
    	}
    }

    //se ho ricevuto una put prendo il file
	if(strncmp(comm, "put", 3) == 0){
		 get_file_server(comm,sockfd,r,msg.s);
	}

	//get o list --> invio file
	else if(  (strncmp(comm,"list",4) == 0) || (strncmp(comm,"get",3) == 0)  ){
		send_file_server(comm,sockfd,r,msg.s);
	}

	printf("end request\n");

}




void get_semaphore(sem_t* sem)
{
	//decremento il valore puntato da sem-->se =0 blocco e attendo
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
	Pkt_head p;
	struct sockaddr_in addr;
	int sockfd;
	struct msgbuf msg;

	msg.mtype = 1;
	Manage_request* pr = get_shared_memory(sid);

	while(n_req < 5){
		//controllo se ho ricevuto messaggi sulla coda di messaggi
		if(msgrcv(qid,&msg,	sizeof(struct sockaddr_in) + sizeof(int),1,0) == -1)
			err_exit("msgrcv");
		//prendo il semaforo
		get_semaphore(&pr->sem);
		//decremento i processi avviabili-->ho preso il semaforo
		--(pr->n_avail);
		//rilascio il semaforo
		release_semaphore(&pr->sem);

		//setto memoria della struct sockaddr_in a 0
		memset((void *)&addr,0, sizeof(addr));
		//riempo la struttura
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		
		addr.sin_port = htons(0);
		//inizializzo socket
		initialize_socket(&sockfd,&addr);

		//all'interno del Pkt_head p metto come nseq -1 e come n_ack quello del client che mi ha contattato(il suo nseq)
		p.n_seq = -1;
		p.n_ack = msg.client_seq;
		//invio sulla socket creata del figlio
		//il valore 0 --> corrisponde all'ack delal connessione
		if (sendto(sockfd, &p, sizeof(Pkt_head), 0, (struct sockaddr *)&msg.s, sizeof(msg.s)) < 0)			
				err_exit("sendto");
		//connessione effettuata-->posso gestire l'operazione richiesta che è specificata in msg
		manage_client(sockfd,msg);		
		//finito di gestire-->incremento nreq				
		++n_req;
		//prendo il semaforo per aggiunarnare la variabile di processi ora avviabili
			//ho finito il lavoro-->sono di nuovo avviabilie
		get_semaphore(&pr->sem);
		++(pr->n_avail);
		release_semaphore(&pr->sem);
	}
	//ho finito(ho fatto 5 richeiste)-->prendo semaforo e decremento processi disponibili
	get_semaphore(&pr->sem);
	--(pr->n_avail);
	release_semaphore(&pr->sem);

	exit(EXIT_SUCCESS);
}





void initialize_processes(int qid,int sid)
{
	//creo 10 figli e gli assegno i lavoro 
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





void write_on_queue(int qid,struct sockaddr_in s,Pkt_head p)
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
			Manage_request* pr = get_shared_memory(sid);
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

  //socket ascolto server
  int sockfd;
  clearScreen();
  int sid;
  //struttura dati per gestire informazioni client
  socklen_t len;
  struct sockaddr_in addr;
  //Struttura dati per pkt
  Pkt_head p;
  //Usato per gestire figli zombie -->struttura da riempire in sighchild
  struct sigaction sa;


  //Gestisco figli zombie 
  handle_sigchild(&sa);					

  //Dimensione della finestra-->viene settata di default in un range 80-93(valori ottimali a livello di prestazioni)
  if(DIMWIN <= 0)
	  n_win = 80;
  if(DIMWIN > 93)
	  n_win = 93;
  else
	  n_win = DIMWIN;

  //Per vedere se il timeout impostato è adattativo o meno
  if(ADAPTATIVE != 1)
	  adaptive = 0;
  else
	  adaptive = 1;
  srand(time(NULL));

  /*
  Creo una coda di messaggi dove il client invia i dati ai processi figli
  	-->Invia ip e port del client che ha contattato-->dati messi in sockaddr_in
   */
  int qid = get_msg_queue();


  //creo memoria condivisa-->prendo id memoria condivisa  
   
  sid = get_memid();
  Manage_request* pr = get_shared_memory(sid);

  /*
	Inizializzo il semaforo contenuto nella struttura dati Manage_request
		inizializzo valore ad 1 con int pshared =1 --> semaforo condiviso tra processi->deve stare all'interno
			di una regione di memoria condivisa
  */
  if(sem_init(&pr->sem,1,1) == -1)
	  err_exit("sem init");

  //numero iniziale di processi --> quanti processi creo
  pr->n_avail = 10;						

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

	  //se numero di processi è minore di 5-->creo altri 5 figli
	  if(pr->n_avail <5)

		create_new_processes(qid,sid);
  }

  wait(NULL);
  return 0;
}
