include ../sigma-sdk.mk

CFLAGS+= -isystem/${BUILDPREFIX}/include/directfb
all: imagetest

imagetest: imagetest.c
	$(CXX) -g $(CFLAGS) $(LDFLAGS) -o imagetest -ldirectfb -ldirect -lfusion imagetest.c

clean:
	rm -f imagetest

