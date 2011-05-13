DCCHD_DIR=/home/davilla/xbmc-sigma/trunk/build/tmp/work/smp8656-linux-gnu/dcchd-smp865x-black-3.10.0-r1/dcchd_SMP865x_3_10_0_black.mips/dcchd

CXXFLAGS+= -isystem$(DCCHD_DIR)/core -isystem$(DCCHD_DIR)/mono

SRCS=	\
	FileIDataSource.cpp \
	SMPPlayer.cpp

LIB=	SMPPlayer.a

include /home/davilla/xbmc-sigma-smp/Makefile.include
-include $(patsubst %.cpp,%.P,$(patsubst %.c,%.P,$(SRCS)))

