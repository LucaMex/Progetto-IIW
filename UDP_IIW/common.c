#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include"common.h"
#include <string.h>
#define SERVPORT 5194
#include "data_types.h"


void err_exit(char* str)
{
    fprintf(stderr,ANSI_COLOR_RED "%s\n" ANSI_COLOR_RESET,str );
    exit(EXIT_FAILURE);
}


void clearScreen()
{
  const char *CLEAR_SCREEN_ANSI = "\e[1;1H\e[2J";
  if(write(STDOUT_FILENO, CLEAR_SCREEN_ANSI,strlen(CLEAR_SCREEN_ANSI))==-1){

  	err_exit("ClearScreen");
  }
  printf(">");
}

int convert_in_int(char* str)
{
	int v;
	char* p;
	errno =0 ;
	printf("str = %s\n",str);
	v = strtol(str,&p,0);
	if((errno != 0) || (*p != '\0'))
		return -1;
	return v;
}




int open_file(char* filename,int flags)
{
	int fd;
	if((fd = open(filename, flags)) == -1){
		err_exit("open");
	}
	return fd;

}


void close_file(int fd)
{
	if(close(fd) == -1)
		err_exit("closing file");
}




char* read_from_stdin()
{
	char* line = malloc(MAXLINE*sizeof(char));
	if(line == NULL)
		err_exit("malloc");
	line = fgets(line,MAXLINE,stdin);
	if(line == NULL){
		if(errno == EINTR)
			printf("eintr\n");
		if(feof(stdin))
			return NULL;
		err_exit("fgets");
	}
	if(line[strlen(line) - 1] == '\n')
		line[strlen(line) - 1] = '\0';
	return line;
}



off_t get_file_len(int fd)
{
	off_t len = lseek(fd,0,SEEK_END);		//get n. bytes file
	if(len == -1)
		err_exit("lseek");

	if(lseek(fd,0,SEEK_SET)  == -1)
		err_exit("lseek");

	return len;
}



int generate_casual()
{
    int x = random()%1000 + 1;      //number between 1 and 1000
    return x;
}


void initialize_addr(struct sockaddr_in* s)
{
	 struct sockaddr_in addr = *s;
	 memset((void *)&addr, 0, sizeof(addr));
	 addr.sin_family = AF_INET;
	 addr.sin_addr.s_addr = htonl(INADDR_ANY);
	 addr.sin_port = htons(SERVPORT);
	 *s = addr;

}




char* write_pathname(int len,const char*path,char*filename)
{
	char* buffer = malloc(len*sizeof(char));
	if(buffer == NULL)
		err_exit("malloc");
	unsigned int i;
	//copio dentro buffer il path
	for(i=0;i<strlen(path);i++){
		*(buffer+i) = *(path+i);
	}

	unsigned int j;
	//aggiunto il file name
	for(j=0;j<strlen(filename);i++,j++){
		*(buffer + i ) = *(filename + j);
	}

	*(buffer + i) = '\0';
	return buffer;
}





char* get_file_path(char* filename,const char* directory)
{
	int l_path = strlen(directory);
	int l_name = strlen(filename);
	//creo il path
    char* new_path = write_pathname(l_path + l_name + 1,directory,filename);
	return new_path;
}






int existing_file(char* filename,const char* directory)
{
   char* new_path = get_file_path(filename,directory);
   if(access(new_path,F_OK)!= 0)			/*not existing file*/
	   return 0;
	return 1;
}



int get_n_packets(off_t len)
{
	int n_packets;
	if((len%MAXLINE) == 0)
		n_packets = len/MAXLINE;
	else
		n_packets = (len/MAXLINE) + 1;
	return n_packets;
}





off_t conv_in_off_t(char data[])
{
	off_t ret;
	char* p;
	unsigned int v;


	errno = 0;
	v = strtoul(data,&p,0);

	if(errno != 0 || *p != '\0')
		err_exit("strtoul");

	ret = v;


	return ret;
}


int read_file(char* buffer,int fd,int max_bytes)
{
	int r,tot = 0;
	for(;;){
		//finche non ho letto il max-->continuo-->classica funzione di SO 
		if(tot == max_bytes)
			break;
		r = read(fd,buffer+tot,max_bytes-tot);
		if(r == -1)
			perror("read");
		if(r == 0)
			break;
		tot += r;
	}
	return tot;
}


void write_on_file(char buffer[],int fd, int n_bytes)
{
	int v,tot = 0;

	for(;;){

		if(tot == n_bytes)
			break;

		v = write(fd,buffer+tot,n_bytes-tot);
		if(v == -1)
			perror("write");

		tot+=v;
	}
}





int get_n_bytes(off_t len,int tot_read)
{
	//è una saturazione dei byte
	int n_bytes;
	if(len-tot_read < MAXLINE)
		n_bytes = len - tot_read;
	else
		n_bytes = MAXLINE;

	return n_bytes;
}


void copy_data(char dest[],char* src, int n_bytes)
{
	int i,k=0;

	for(i = 0; i < n_bytes; i++,k++){
		dest[k] = src[i];
	}
}

void write_file_len(off_t* len,int* fd,Pkt_head* p, char* filename,const char* directory)
{
	char* file_path = get_file_path(filename,directory);
	*fd = open_file(file_path,O_RDONLY);
	*len = get_file_len(*fd);
	sprintf(p->data,"%zu",*len);
}

//se servDir non esiste la creo nella cartella corrente del programma ed anche list_file.txt
void initialize_fold(const char* directory) 
{
	struct stat st = {0};

	if (stat(directory, &st) == -1){ //se non c'è la creo
		//creo la directory con 0700 permessi di accesso
		if(mkdir(directory, 0700) == -1)
			err_exit("mkdir\n");
	}
	return;
}

//scarica  file, se tutto bene ritorna 0 e il file è chiuso, senno -1; va passato l'ack attuale  della comunicazione
int create_file(char* filename,const char* directory) 
{
	int fd;
	char* new_path;
	//prendo il path a partire da filename e directory
	new_path = get_file_path(filename,directory);
	//creo la cartella se non esiste(anche il file list.txt)--> vedi commenti funzione initialize_fold
	initialize_fold(directory);
	//prendo il file descriptor del file
	fd = open(new_path, O_CREAT | O_EXCL | O_WRONLY,0733);
	if (fd == -1) {
		if(errno == EEXIST){
			return -1;
		}
		else
			err_exit("open");
	}else{
		fflush(stdout);
	}
	return fd;
}




int file_lock(int fd, int cmd)
{
	int result;
	struct flock fl;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_type = (cmd == LOCK_EX ? F_WRLCK :
		     (cmd == LOCK_SH ? F_RDLCK : F_UNLCK));


	result =fcntl(fd, F_SETLKW, &fl);
	if(result == -1)
		err_exit("fcntl");
	return result;
}


int locked_file(int fd)
{
	struct flock fd_lock = {F_RDLCK, SEEK_SET,   0,      0,     0 };
	if(fcntl(fd, F_GETLK, &fd_lock) == -1) {
		err_exit("fcntl");
	}
	if(fd_lock.l_type == F_UNLCK)
		return 0;
	return 1;
}




