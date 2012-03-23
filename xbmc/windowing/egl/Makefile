INCLUDES=-I. -I../.. -I../../linux

SRCS = WinSystemGLES.cpp
SRCS+= WinEGLPlatformGeneric.cpp
SRCS+= WinEGLPlatformRaspberryPI.cpp

LIB=windowing_egl.a

include ../../../Makefile.include
-include $(patsubst %.cpp,%.P,$(patsubst %.c,%.P,$(SRCS)))
