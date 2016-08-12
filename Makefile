PROGS      = camogm
PHPFILES   = camogmstate.php
CONFIGS    = qt_source

SRCS = camogm.c camogm_ogm.c camogm_jpeg.c camogm_mov.c camogm_kml.c camogm_read.c index_list.c
OBJS = camogm.o camogm_ogm.o camogm_jpeg.o camogm_mov.o camogm_kml.o camogm_read.o index_list.o

CFLAGS    += -Wall -I$(STAGING_KERNEL_DIR)/include/elphel
LDLIBS    += -logg -pthread -lm

SYSCONFDIR = /etc/
BINDIR     = /usr/bin/
WWW_PAGES  = /www/pages

all: $(PROGS)

$(PROGS): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

install: 
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 -t $(DESTDIR)$(BINDIR) $(PROGS)
	install -d $(DESTDIR)$(SYSCONFDIR)
	install -m 0644 -t $(DESTDIR)$(SYSCONFDIR) $(CONFIGS)

clean:
	rm -rf $(PROGS) *.o *~
	
