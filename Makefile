ROOTDIR=$(shell pwd)
SRCDIR=src
TSTDIR=tst
INSTALLDIR=install
LIBDIR=$(INSTALLDIR)/lib
BINDIR=$(INSTALLDIR)/bin
OBJDIR=$(INSTALLDIR)/obj

CC=gcc
CFLAGS=-Wall -Werror -g
CPPFLAGS=-I$(SRCDIR) -I$(TSTDIR)
LDFLAGS=-L$(ROOTDIR)/install/lib
LDLIBS=-lthread
VALGRIND=valgrind --leak-check=full --show-reachable=yes --track-origins=yes

TST=$(addprefix $(BINDIR)/, $(patsubst %.c,%,$(shell ls $(ROOTDIR)/$(TSTDIR))))
PTHREAD_TST=$(addsuffix -pthread, $(TST))
get_args=$(shell cat tests.csv | grep $(patsubst $(BINDIR)/%,%,$(1)) | cut -d ";" -f 2)

.PHONY : all install check valgrind

all : install

install  : threads pthreads

$(OBJDIR)/%.o : $(SRCDIR)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJDIR)/thread.o : $(SRCDIR)/thread.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

$(OBJDIR)/%-pthread.o : $(TSTDIR)/%.c
	$(CC) -DUSE_PTHREAD $(CPPFLAGS) $(CFLAGS) -c $< -o $@
	
$(OBJDIR)/%.o : $(TSTDIR)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIBDIR)/libthread.so : $(OBJDIR)/thread.o
	$(CC) --shared -o $@ $<

check : install
	$(foreach var,$(TST), echo "Test de $(var) avec $(call get_args, $(var)) :"; LD_LIBRARY_PATH=./$(LIBDIR) ./$(var) $(call get_args,$(var)) ;)

valgrind : install
	$(foreach var,$(TST), LD_LIBRARY_PATH=./$(LIBDIR) $(VALGRIND) ./$(var) $(call get_args,$(var));)

$(TST) : $(BINDIR)/% : $(OBJDIR)/%.o $(LIBDIR)/libthread.so 
	$(CC) $(CFLAGS) $(LDLIBS) $(LDFLAGS) $(LIBDIR)/libthread.so $< -o $@

$(PTHREAD_TST) : $(BINDIR)/%-pthread : $(OBJDIR)/%-pthread.o
	$(CC) $(CFLAGS) -DUSE_PTHREAD -lpthread $< -o $@

threads : $(TST)
	
pthreads : $(PTHREAD_TST) 

graph :

clean : 
	rm -R ${BINDIR}/* ${LIBDIR}/* ${OBJDIR}/*