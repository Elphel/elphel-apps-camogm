GUIDIR     = camogmgui
PROGS      = camogm
TEST_PROG  = camogm_test 
TEST_PROG1  = camogm_fifo_writer
TEST_PROG2  = camogm_fifo_reader
 
PHPSCRIPTS = camogmstate.php $(GUIDIR)/camogmgui.php $(GUIDIR)/camogmgui.css $(GUIDIR)/camogmgui.js $(GUIDIR)/camogm_interface.php \
             $(GUIDIR)/SpryTabbedPanels.css $(GUIDIR)/SpryTabbedPanels.js $(GUIDIR)/xml_simple.php $(GUIDIR)/SpryCollapsiblePanel.css \
             $(GUIDIR)/SpryCollapsiblePanel.js
CONFIGS    = qt_source
IMAGES     = $(GUIDIR)/images/filebrowser-01.gif $(GUIDIR)/images/filebrowser-bottom-01.gif $(GUIDIR)/images/png_white_30.png \
             $(GUIDIR)/images/record.gif $(GUIDIR)/images/reload.png $(GUIDIR)/images/stop.gif $(GUIDIR)/images/create_folder.png \
             $(GUIDIR)/images/divider.png $(GUIDIR)/images/folder.gif $(GUIDIR)/images/help.png $(GUIDIR)/images/quicktime.png \
             $(GUIDIR)/images/rec_folder.png $(GUIDIR)/images/up_folder.gif $(GUIDIR)/images/play_audio.png $(GUIDIR)/images/hdd.png


SRCS = camogm.c camogm_ogm.c camogm_jpeg.c camogm_mov.c camogm_kml.c camogm_read.c index_list.c camogm_align.c
TEST_SRC = camogm_test.c 
TEST_SRC1 = camogm_fifo_writer.c 
TEST_SRC2 = camogm_fifo_reader.c

OBJS = $(SRCS:.c=.o)

CFLAGS    += -Wall -I$(STAGING_DIR_HOST)/usr/include-uapi
LDLIBS    += -logg -pthread -lm

INSTALL    = install
INSTMODE   = 0755
INSTDOCS   = 0644
OWN        = -o root -g root

SYSCONFDIR = /etc/
BINDIR     = /usr/bin/
WWW_PAGES  = /www/pages
IMAGEDIR   = $(WWW_PAGES)/images

all: $(PROGS) $(TEST_PROG) $(TEST_PROG1) $(TEST_PROG2)

$(PROGS): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST_PROG): $(TEST_SRC:.c=.o) 
$(TEST_PROG1): $(TEST_SRC1:.c=.o)
$(TEST_PROG2): $(TEST_SRC2:.c=.o)

install: $(PROGS) $(PHPSCRIPTS) $(CONFIGS)
	$(INSTALL) $(OWN) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) $(OWN) -m $(INSTMODE) $(PROGS)      $(DESTDIR)$(BINDIR)
	$(INSTALL) $(OWN) -m $(INSTMODE) $(TEST_PROG)  $(DESTDIR)$(BINDIR)
	$(INSTALL) $(OWN) -m $(INSTMODE) $(TEST_PROG1)  $(DESTDIR)$(BINDIR)
	$(INSTALL) $(OWN) -m $(INSTMODE) $(TEST_PROG2)  $(DESTDIR)$(BINDIR)
	$(INSTALL) $(OWN) -d $(DESTDIR)$(SYSCONFDIR)
	$(INSTALL) $(OWN) -m $(INSTDOCS) $(CONFIGS)    $(DESTDIR)$(SYSCONFDIR)
	$(INSTALL) $(OWN) -d $(DESTDIR)$(WWW_PAGES)
	$(INSTALL) $(OWN) -m $(INSTMODE) $(PHPSCRIPTS) $(DESTDIR)$(WWW_PAGES)
	# install camogmgui
	$(INSTALL) $(OWN) -d $(DESTDIR)$(IMAGEDIR)
	$(INSTALL) $(OWN) -m $(INSTMODE) $(IMAGES)     $(DESTDIR)$(IMAGEDIR)

clean:
	rm -rf $(PROGS) *.o *~ .depend
	
depend: .depend

.depend: $(SRCS) $(TEST_SRCS)
	rm -f ./.depend
	$(CC) $(CFLAGS) -MM $^ > ./.depend;

include .depend
