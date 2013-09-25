#You can use either a gcc or g++ compiler
#CC = g++
CC = gcc
EXECUTABLES = dsh
CFLAGS = -I. -Wall -DNDEBUG
#Disable the -DNDEBUG flag for the printing the freelist
#CFLAGS = -I. -Wall
PTFLAG = -O2
DEBUGFLAG = -g3

all: CFLAGS += ${DEBUGFLAG}
all: ${EXECUTABLES}

test: CFLAGS += $(OPTFLAG)
test: ${EXECUTABLES}
	for exec in ${EXECUTABLES}; do \
		./$$exec ; \
	done

#debug: CFLAGS += $(DEBUGFLAG)
debug: $(EXECUTABLES)
	for dbg in ${EXECUTABLES}; do \
        	gdb ./$$dbg ; \
	done

dsh: dsh.c parse.c helper.c dsh.h
	$(CC) $(CFLAGS) -o dsh dsh.c parse.c helper.c

#dsh: dsh.c dsh.h
#	$(CC) $(CFLAGS) -o dsh dsh.c
clean:
	rm -f ${EXECUTABLES} *.o *~
