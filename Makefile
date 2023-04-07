SRCDIR=src
TSTDIR=tst
INSTALLDIR=install
LIBDIR=$(INSTALLDIR)/lib
BINDIR=$(INSTALLDIR)/bin
OBJDIR=$(INSTALLDIR)/obj

CC=gcc
CFLAGS=-Wall -Werror -g
CPPFLAGS=-I${SRCDIR} -I${TSTDIR}

BIN= thread example

$(OBJDIR)/%.o : $(SRCDIR)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o : $(TSTDIR)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

all : install 

check : ${BINDIR}/*
	./$^

valgrind : ${BINDIR}/*
	valgrind ./$^
	
pthreads :

graph :

install : $(addprefix $(OBJDIR)/, $(addsuffix .o, $(BIN)))
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o ${BINDIR}/example
