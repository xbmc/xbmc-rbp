include ../sigma-sdk.mk

BIN = smp_decoder

INCLUDES+= -I./ffmpeg -I$(DCCHD_DIR)/core -I$(DCCHD_DIR)/dcchd -I$(DCCHD_DIR)/mono -I${PREFIX}/include/directfb

LDFLAGS+= -lbz2 -lz -lc -lpthread -ldirectfb -lfusion -ldirect \
	-L$(PREFIX)/lib \
	-Wl,-rpath-link -Wl,$(RUA_DIR)/lib \
	-Wl,-rpath-link -Wl,$(DCCHD_DIR)/core \
	-Wl,-rpath-link -Wl,$(DCCHD_DIR)/mono \
	-Wl,-rpath-link -Wl,$(DCCHD_DIR)/dcchd
CPPFLAGS+= -g -O0 -Wall -D__STDC_CONSTANT_MACROS
CPPFLAGS+= ${INCLUDES}

SRCFILES = smp_decoder.cpp \
           file_reader_util.cpp \
           bitstream_converter.cpp \
           ffmpeg_file_protocol.cpp

OBJFILES = ${SRCFILES:.cpp=.o}

FFMPEG_LIBS = \
           ffmpeg/libavformat/libavformat.a \
           ffmpeg/libavcodec/libavcodec.a \
           ffmpeg/libavutil/libavutil.a \
              

all:: ffmpeg-libs $(BIN)

$(BIN): $(OBJFILES)
	$(CXX)  $(LDFLAGS) -o $(BIN) $(OBJFILES) $(FFMPEG_LIBS)

ffmpeg-libs: ffmpeg/config.h
	make -C ffmpeg

ffmpeg/config.h:
	rm -rf ffmpeg
	tar xf ffmpeg-0.6.1.tar.gz; mv ffmpeg-0.6.1 ffmpeg
	cd ffmpeg && patch -p1 <../03_mpeg4_video_to_elementary_stream.patch
	cd ffmpeg && patch -p1 <../04_vc1_bsfs.patch
	cd ffmpeg && ./configure \
      --extra-cflags="$(CFLAGS) -D__STDC_CONSTANT_MACROS" \
      --enable-cross-compile \
      --arch=mips \
      --target-os=linux \
      --enable-static \
      --disable-shared \
      --disable-muxers \
      --enable-muxer=spdif \
      --enable-muxer=adts \
      --disable-encoders \
      --enable-encoder=ac3 \
      --enable-encoder=aac \
      --disable-devices \
      --disable-ffplay \
      --disable-ffprobe \
      --disable-ffserver \
      --disable-ffmpeg \
      --enable-shared \
      --disable-decoder=mpeg_xvmc \
      --enable-postproc \
      --enable-gpl \
      --enable-protocol=http \
      --enable-pthreads \
      --cc="$(CC)"

ffmpeg-clean:
	make -C ffmpeg clean

clean::
	rm -f $(BIN) $(OBJFILES)

distclean:: clean
	rm -rf ffmpeg	



