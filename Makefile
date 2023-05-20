ROOTDIR=$(shell pwd)
SRCDIR=src
TSTDIR=tst
INSTALLDIR=install
LIBDIR=$(INSTALLDIR)/lib
BINDIR=$(INSTALLDIR)/bin
OBJDIR=$(INSTALLDIR)/obj

CC=gcc
CFLAGS=-Wall -g
CPPFLAGS=-I$(SRCDIR) -I$(TSTDIR) $(DEBUG) $(NOASSERT) $(DEADLOCK)
LDFLAGS=-L$(ROOTDIR)/install/lib
LDLIBS=-lthread
VALGRIND=valgrind --leak-check=full --show-reachable=yes --track-origins=yes

GRAPH_FILES?=
DEADLOCK?=
DEBUG?=
NOASSERT?= -DNDEBUG

TST=$(addprefix $(BINDIR)/, $(shell /usr/bin/cat tests.csv | cut -d ";" -f 1))
PTHREAD_TST=$(addsuffix -pthread, $(TST))
get_args=$(shell cat tests.csv | grep $(patsubst $(BINDIR)/%,%,$(1)) | cut -d ";" -f 2)

.PHONY : all install check valgrind

all : install

install  :  threads pthreads

$(BINDIR) $(LIBDIR) :
	mkdir -p $@ $(OBJDIR)

$(OBJDIR)/%.o : $(SRCDIR)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJDIR)/utils.o : $(SRCDIR)/utils.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

$(OBJDIR)/thread.o : $(SRCDIR)/thread.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

$(OBJDIR)/adj_list.o : $(SRCDIR)/adj_list.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

$(OBJDIR)/%-pthread.o : $(TSTDIR)/%.c
	$(CC) -DUSE_PTHREAD $(CPPFLAGS) $(CFLAGS) -c $< -o $@
	
$(OBJDIR)/%.o : $(TSTDIR)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIBDIR)/libthread.so : $(OBJDIR)/thread.o $(OBJDIR)/utils.o $(OBJDIR)/adj_list.o $(LIBDIR)
	$(CC) --shared -o $@ $(OBJDIR)/thread.o $(OBJDIR)/utils.o $(OBJDIR)/adj_list.o

check : install
	$(foreach var,$(TST), echo "Test de $(var) avec $(call get_args, $(var)) :"; LD_LIBRARY_PATH=./$(LIBDIR) ./$(var) $(call get_args,$(var)) ;)

valgrind : install
	$(foreach var,$(TST), LD_LIBRARY_PATH=./$(LIBDIR) $(VALGRIND) ./$(var) $(call get_args,$(var));)

$(TST) : $(BINDIR)/% : $(OBJDIR)/%.o $(LIBDIR)/libthread.so 
	$(CC) $(CFLAGS) $(LDFLAGS) $(LIBDIR)/libthread.so $< -o $@ $(LDLIBS) 

$(PTHREAD_TST) : $(BINDIR)/%-pthread : $(OBJDIR)/%-pthread.o
	$(CC) $(CFLAGS) -DUSE_PTHREAD $< -o $@ -lpthread

threads : $(BINDIR) $(TST)
	
pthreads : $(BINDIR) $(PTHREAD_TST) 

graphs : threads pthreads
	python3 graphs.py $(GRAPH_FILES)

clean : 
	rm -R -f ${BINDIR}/* ${LIBDIR}/* ${OBJDIR}/*