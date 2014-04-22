KANNELCONF = gw-config
PGCONF = pg_config
#MBUNIDIR = ../
INCS =  -I/usr/local/include
CFLAGS = -Wno-deprecated-declarations  -Wall -fPIC  -g  `$(KANNELCONF) --cflags | sed 's/-O.//g'` -I`$(PGCONF) --includedir`
LDFLAGS= -L`$(PGCONF) --libdir` -lpq -lssl
CC = gcc -g  -Wall 
SRCS = dispatcher2.c request_processor.c
OBJS = $(SRCS:.c=.o)

APP = dispatcher2

$(APP): $(OBJS)
	$(CC)  -o $@ $(OBJS)  `$(KANNELCONF) --libs` $(LDFLAGS)

.c.o:
	$(CC) $(INCS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *~ $(OBJS)

clobber: clean
	rm -f $(APP)






