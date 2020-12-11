SHELL		= /bin/sh

TARGET		= libuthread.so

OFILES		= uthread.o \
			  uthread_ctx.o uthread_queue.o uthread_mtx.o \
			  uthread_cond.o uthread_sched.o 

HANDIN		= snarf.tar

# user executables, test code... wowza
EXECS		= test.o basic_test.o

CC			= gcc

CFLAGS		= -g -Wall -Werror -fPIC -D_GNU_SOURCE
IFLAGS		=
LFLAGS		= -L. -Wl,--rpath,. 

LIBS		= -ldl

.PHONY: all cscope clean handin

all: cscope $(TARGET) $(EXECS)
	@for exec in $(EXECS); do \
		$(CC) $(CFLAGS) $(LFLAGS) -luthread -o `basename $$exec .o` $$exec; \
	done \

cscope:
	@find -name "*.[chS]" > cscope.files
	cscope -k -b -q -v

$(TARGET): $(OFILES)
	$(CC) -g -shared $(LFLAGS) -o $(TARGET) $(OFILES) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(IFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) *.o
	rm -f cscope.files cscope.out cscope.in.out cscope.po.out
	for exec in $(EXECS) ; do \
		if [ -f `basename $$exec .o` ] ; then \
			rm `basename $$exec .o` ; \
		fi \
	done