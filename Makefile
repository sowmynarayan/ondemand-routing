CC = gcc

LIBS = /users/cse533/Stevens/unpv13e/libunp.a -lpthread
FLAGS = -g -O2

CFLAGS = ${FLAGS} -Wno-int-to-pointer-cast -I/users/cse533/Stevens/unpv13e/lib

all: client server odr

server: server.o
	${CC} ${FLAGS} -o server_ssrinath server.o ${LIBS}
server.o: server.c common.h api.h
	${CC} ${CFLAGS} -c server.c

client: client.o
	${CC} ${FLAGS} -o client_ssrinath client.o ${LIBS}
client.o: client.c common.h api.h
	${CC} ${CFLAGS} -c client.c

odr: get_hw_addrs.o odr.o
	${CC} ${FLAGS} -o ODR_ssrinath odr.o get_hw_addrs.o ${LIBS}
odr.o: odr.c common.h
	${CC} ${CFLAGS} -c odr.c
get_hw_addrs.o: get_hw_addrs.c hw_addrs.h
	${CC} ${CFLAGS} -c get_hw_addrs.c

clean:  
	rm server_ssrinath server.o client_ssrinath client.o ODR_ssrinath odr.o get_hw_addrs.o
