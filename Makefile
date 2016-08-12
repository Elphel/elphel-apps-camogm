PROGS      = camogm
PHPFILES   = camogmstate.php

SRCS = camogm.c camogm_ogm.c camogm_jpeg.c camogm_mov.c camogm_kml.c camogm_read.c index_list.c
#OBJS = camogm.o camogm_ogm.o camogm_jpeg.o camogm_mov.o camogm_kml.o camogm_read.o index_list.o
OBJS = $(SRC:.c=.o)

#CFLAGS   += -Wall -I$(ELPHEL_KERNEL_DIR)/include/uapi/elphel
CFLAGS   += -Wall -I$(STAGING_DIR_HOST)/usr/include-uapi/elphel
LDLIBS   += -logg -pthread -lm

all: $(PROGS)

$(PROGS): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -rf $(PROGS) *.o *~
#TODO: implement automatic dependencies!
camogm.c:$(STAGING_DIR_HOST)/usr/include-uapi/elphel/x393_devices.h
