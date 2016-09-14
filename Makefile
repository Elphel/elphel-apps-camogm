PROGS      = camogm
PHPFILES   = camogmstate.php
CONFIGS    = qt_source

SRCS = camogm.c camogm_ogm.c camogm_jpeg.c camogm_mov.c camogm_kml.c camogm_read.c index_list.c
OBJS = $(SRCS:.c=.o)

CFLAGS    += -Wall -I$(STAGING_KERNEL_DIR)/include/elphel -I$(STAGING_DIR_HOST)/usr/include-uapi
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
	
