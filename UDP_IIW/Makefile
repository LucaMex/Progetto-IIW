CC = gcc
CFLAGS = -O3 -Wall -Wextra 
DEPS = data_types.h basic.h timer_functions.h common.h thread_functions.h packet_functions.h window_operations.h
COMMONOBJ = timer_functions.o common.o thread_functions.o packet_functions.o window_operations.o

COBJ = client_udp.o
SOBJ = server_udp.o
LIBS = -lrt -pthread 
CEXE = client_udp
SEXE = server_udp

%.o : %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

server : $(COMMONOBJ) $(SOBJ)
	$(CC) $(LIBS) -o $(SEXE) $^ $(CFLAGS) 

client :$(COMMONOBJ) $(COBJ)
	$(CC) $(LIBS) -o $(CEXE) $^ $(CFLAGS)
 

all: server client

.PHONY : clean

clean :
	rm -f $(COBJ) $(SOBJ) $(COMMONOBJ) $(SEXE) $(CEXE)  
