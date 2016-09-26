EXEC = ble_mgr
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c = .o)

RTDIR = $(shell pwd)/..

CFLAGS +=-I$(RTDIR)/lib/libmsgq -g -rdynamic
LDFLAGS +=-lpthread -lrt -L$(RTDIR)/lib/libmsgq -lmsgq

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) 

%.o:%.c
	$(CC) -c $< $(CFLAGS)


clean:
	-rm -f $(EXEC) *.elf *.gdb *.o *~
