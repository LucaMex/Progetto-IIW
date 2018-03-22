#ifndef COMMON_H_H_
#define COMMON_H_H_

//color
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"



#include "basic.h"
#include "data_types.h"


void err_exit(char* str);
int convert_in_int(char* str);
int open_file(char* filename,int flags);
void close_file(int fd);
char* read_from_stdin();
off_t get_file_len(int fd);
int generate_casual();
void initialize_addr(struct sockaddr_in* s);
char* write_pathname(int len,const char*path,char*filename);
char* get_file_path(char* filename,const char* directory);
int existing_file(char* filename,const char* directory);
int get_n_packets(off_t len);
off_t conv_in_off_t(char data[]);
int read_file(char* buffer,int fd,int max_bytes);
void write_on_file(char buffer[],int fd, int n_bytes);
int get_n_bytes(off_t len,int tot_read);
void copy_data(char dest[],char* src, int n_bytes);
void write_file_len(off_t* len,int* fd,Pkt_head* p, char* filename,const char* directory);
void initialize_fold(const char* directory);
int create_file(char* filename,const char* directory);
int file_lock(int fd, int cmd);
int locked_file(int fd);

#endif
