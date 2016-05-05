AXIS_USABLE_LIBS = UCLIBC GLIBC
AXIS_AUTO_DEPEND = yes
#include $(AXIS_TOP_DIR)/tools/build/Rules.axis

INSTDIR    = $(prefix)/usr/local/sbin/
OTHERDIR   = $(prefix)/usr/html/
PHPDIR     = $(prefix)/usr/html/
INSTMODE   = 0755
INSTOTHER  = 0644
INSTPHP    = 0644
INSTOWNER  = root
INSTGROUP  = root

INCDIR     = $(prefix)/include

PROGS      = camogm
PHPFILES   = camogmstate.php


SRCS = camogm.c camogm_ogm.c camogm_jpeg.c camogm_mov.c camogm_kml.c

OBJS = camogm.o camogm_ogm.o camogm_jpeg.o camogm_mov.o camogm_kml.o

CFLAGS   += -Wall -I$(ELPHEL_KERNEL_DIR)/include/elphel
#CFLAGS   += -Wall -I$(INCDIR) -I$(ELPHEL_KERNEL_DIR)/include/elphel

all: $(PROGS)

$(PROGS): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -logg  -o $@
install: $(PROGS)
	$(INSTALL) -d $(INSTDIR)
	$(INSTALL) -m $(INSTMODE) -o $(INSTOWNER) -g $(INSTGROUP) $(PROGS) $(INSTDIR)
#already installed with ccam.cgi
	$(INSTALL) -m $(INSTMODE) -o $(INSTOWNER) -g $(INSTGROUP) qt_source $(prefix)/etc
	$(INSTALL) -m $(INSTPHP)  -o $(INSTOWNER) -g $(INSTGROUP) $(PHPFILES) $(PHPDIR)
clean:
	rm -rf $(PROGS) *.o *~

configsubs:
