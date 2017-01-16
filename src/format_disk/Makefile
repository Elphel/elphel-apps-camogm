BINDIR       = /usr/bin
DOCUMENTROOT = /www/pages
INSTALL      = install
INSTMODE     = 0755
INSTDOCS     = 0644
OWN          = -o root -g root
LN           = ln -sf

# executable scripts will go to $(BINDIR)
EXESCRIPTS   = format_disk.py
DOCS         = format_disk.js format_disk.php

all: $(SCRIPTS) $(DOCS)

install: $(EXESCRIPTS) $(DOCS)
	$(INSTALL) $(OWN) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) $(OWN) -d $(DESTDIR)$(DOCUMENTROOT)
	$(INSTALL) $(OWN) -m $(INSTMODE) $(EXESCRIPTS) $(DESTDIR)$(BINDIR)
	$(INSTALL) $(OWN) -m $(INSTDOCS) $(DOCS) $(DESTDIR)$(DOCUMENTROOT)

clean: