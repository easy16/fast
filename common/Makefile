.SUFFIXES: .c .o

COMPILE = $(CC) -Wall -O2 -DOS_LINUX
#COMPILE = $(CC) -Wall -g -DOS_LINUX -D__DEBUG__
INC_PATH = -I/usr/local/include
LIB_PATH = -L/usr/local/lib
TARGET_PATH = /usr/local/bin

COMMON_LIB =
SHARED_OBJS = hash.o fdfs_define.o chain.o shared_func.o ini_file_reader.o \
              logger.o sockopt.o fdfs_global.o fdfs_base64.o
ALL_OBJS = $(SHARED_OBJS)

ALL_PRGS = 

all: $(ALL_OBJS) $(ALL_PRGS)
.o:
	$(COMPILE) -o $@ $<  $(SHARED_OBJS) $(COMMON_LIB) $(LIB_PATH) $(INC_PATH)
.c:
	$(COMPILE) -o $@ $<  $(ALL_OBJS) $(COMMON_LIB) $(LIB_PATH) $(INC_PATH)
.c.o:
	$(COMPILE) -c -o $@ $<  $(INC_PATH)
install:
	cp -f $(ALL_PRGS) $(TARGET_PATH)
clean:
	rm -f $(ALL_OBJS) $(ALL_PRGS)
