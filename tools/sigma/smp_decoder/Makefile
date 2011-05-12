include ../sigma-sdk.mk

BIN = smp_decoder

INCLUDES+= -I$(DCCHD_DIR)/core -I$(DCCHD_DIR)/dcchd -I$(DCCHD_DIR)/mono -I${BUILDPREFIX}/include/directfb

LDFLAGS+= -lbz2 -lz -lc -lpthread -ldirectfb -lfusion -ldirect \
	-L$(BUILDPREFIX)/lib \
	-Wl,-rpath-link -Wl,$(RUA_DIR)/lib \
	-Wl,-rpath-link -Wl,$(DCCHD_DIR)/core \
	-Wl,-rpath-link -Wl,$(DCCHD_DIR)/mono \
	-Wl,-rpath-link -Wl,$(DCCHD_DIR)/dcchd
CPPFLAGS+= -g -O0 -Wall -D__STDC_CONSTANT_MACROS
CPPFLAGS+= ${INCLUDES}

SRCFILES = \
	CFileIDataSource.cpp \
	smp_decoder.cpp

OBJFILES = ${SRCFILES:.cpp=.o}

all:: $(BIN)

$(BIN): $(OBJFILES)
	$(CXX)  $(LDFLAGS) -o $(BIN) $(OBJFILES)

clean::
	rm -f $(BIN) $(OBJFILES)



