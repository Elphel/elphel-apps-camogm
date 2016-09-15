PROGS      = camogm
PHPSCRIPTS = camogmstate.php
CONFIGS    = qt_source

SRCS = camogm.c camogm_ogm.c camogm_jpeg.c camogm_mov.c camogm_kml.c camogm_read.c index_list.c
OBJS = $(SRCS:.c=.o)

CFLAGS    += -Wall -I$(STAGING_KERNEL_DIR)/include/elphel -I$(STAGING_DIR_HOST)/usr/include-uapi
LDLIBS    += -logg -pthread -lm

INSTALL    = install
INSTMODE   = 0755
INSTDOCS   = 0644
OWN        = -o root -g root

SYSCONFDIR = /etc/
BINDIR     = /usr/bin/
WWW_PAGES  = /www/pages

all: $(PROGS)

$(PROGS): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

install: $(PROGS) $(PHPSCRIPTS) $(CONFIGS)
	$(INSTALL) $(OWN) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) $(OWN) -m $(INSTMODE) $(PROGS)      $(DESTDIR)$(BINDIR)
	$(INSTALL) $(OWN) -d $(DESTDIR)$(SYSCONFDIR)
	$(INSTALL) $(OWN) -m $(INSTDOCS) $(CONFIGS)    $(DESTDIR)$(SYSCONFDIR)
	$(INSTALL) $(OWN) -d $(DESTDIR)$(WWW_PAGES)
	$(INSTALL) $(OWN) -m $(INSTMODE) $(PHPSCRIPTS) $(DESTDIR)$(WWW_PAGES)

clean:
	rm -rf $(PROGS) *.o *~
#TODO: implement automatic dependencies!
camogm.c:$(STAGING_DIR_HOST)/usr/include-uapi/elphel/x393_devices.h
