# For multiple programs using a single source file each,
# we can just define 'progs' and create custom targets.
PROGS	=	fastrpc
LIBNETMAP =

CLEANFILES = $(PROGS) *.o

NMDIR ?= ./netmap/
# VPATH = $(SRCDIR)/apps/pkt-gen

# NO_MAN=
CFLAGS = -O2 -pipe
CFLAGS += -Werror -Wall -Wunused-function
CFLAGS += -I $(NMDIR)/sys -I $(NMDIR)/libnetmap
CFLAGS += -Wextra -Wno-address-of-packed-member

LDFLAGS += -L $(NMDIR)/build-libnetmap
LDLIBS += -lpthread -lm -lnetmap
ifeq ($(shell uname),Linux)
LDLIBS += -lrt	# on linux
endif

all: $(PROGS)

clean:
	-@rm -rf $(CLEANFILES)

fastrpc: fastrpc.o

fastrpc.o: fastrpc.c
	$(CC) $(CFLAGS) -c $^ -o $@
