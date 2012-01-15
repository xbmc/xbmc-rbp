/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#ifndef _OMX_READER_H_
#define _OMX_READER_H_

#include "OMXThread.h"
#include "utils/StdString.h"
#include "DllAvUtil.h"
#include "DllAvFormat.h"
#include "DllAvFilter.h"
#include "DllAvCodec.h"
#include "DllAvCore.h"
#include "OMXStreamInfo.h"
#include <queue>

#include "OMXStreamInfo.h"

#ifdef STANDALONE
#include "File.h"
#else
#include "xbmc/filesystem/File.h"
#endif

#include <sys/types.h>

using namespace XFILE;
using namespace std;

#define MAX_OMX_CHAPTERS 64

#define MAX_OMX_AUDIO_PACKETS  200
#define MAX_OMX_VIDEO_PACKETS  100

#define MAX_OMX_STREAMS        100

#define OMX_PLAYSPEED_PAUSE  0
#define OMX_PLAYSPEED_NORMAL 1

#ifndef FFMPEG_FILE_BUFFER_SIZE
#define FFMPEG_FILE_BUFFER_SIZE   32768 // default reading size for ffmpeg
#endif
#ifndef MAX_STREAMS
#define MAX_STREAMS 100
#endif

typedef struct OMXChapter
{
  std::string name;
  int64_t     seekto_ms;
  double      ts;
} OMXChapter;

class OMXPacket : public COMXStreamInfo
{
public:
  double    pts; // pts in DVD_TIME_BASE
  double    dts; // dts in DVD_TIME_BASE
  double    duration; // duration in DVD_TIME_BASE if available
  int       size;
  uint8_t   *data;
  int       stream_index;
};

class OMXReader : public OMXThread
{
protected:
  AVStream                  *m_pVideoStream;
  AVStream                  *m_pAudioStream;
  int                       m_video_index;
  int                       m_video_count;
  int                       m_audio_index;
  int                       m_audio_count;
  std::vector<AVStream*>    m_video_streams;
  std::vector<AVStream*>    m_audio_streams;
  std::queue<OMXPacket *>   m_pkt_video;
  std::queue<OMXPacket *>   m_pkt_audio;
  DllAvUtil                 m_dllAvUtil;
  DllAvCodec                m_dllAvCodec;
  DllAvFormat               m_dllAvFormat;
  bool                      m_open;
  CStdString                m_filename;
  bool                      m_bMatroska;
  bool                      m_bAVI;
  bool                      m_bMpeg;
  XFILE::CFile              *m_pFile;
  AVFormatContext           *m_pFormatContext;
  ByteIOContext             *m_ioContext;
  bool                      m_eof;
  OMXChapter                m_chapters[MAX_OMX_CHAPTERS];
  int                       m_chapter_count;
  COMXStreamInfo            m_hints_audio;
  COMXStreamInfo            m_hints_video;
  double                    m_iCurrentPts;
  pthread_cond_t            m_packet_buffer_cond;
  int64_t                   m_seek_ms;
  int                       m_seek_req;
  int                       m_seek_flags;
  int                       m_speed;
  int64_t                   m_duration_ms;
  void AddTimespecs(struct timespec &time, long millisecs);
#ifdef STANDALONE
  void flush_packet_queue(AVFormatContext *s);
  void av_read_frame_flush(AVFormatContext *s);
#endif
  void FlushVideoPackets();
  void FlushAudioPackets();
private:
public:
  OMXReader();
  ~OMXReader();
  bool Open(CStdString filename, bool dump_format);
  bool Close();
  void FlushRead();
  bool SeekTime(int64_t seek_ms, int seek_flags, double *startpts);
  void Process();
  bool GetStreams();
  bool GetHints(AVStream *stream, COMXStreamInfo *hints);
  bool IsEof() { return m_eof; };
  int  GetVideoPacketsFree();
  int  GetAudioPacketsFree();
  OMXPacket *GetVideoPacket();
  OMXPacket *GetAudioPacket();
  COMXStreamInfo  GetVideoHints(int index);
  COMXStreamInfo  GetAudioHints(int index);
  COMXStreamInfo  GetVideoHints() { return m_hints_video; };
  COMXStreamInfo  GetAudioHints() { return m_hints_audio; };
  int  AudioStreamCount() { return m_audio_count; };
  int  VideoStreamCount() { return m_video_count; };
  AVStream *GetVideoStream() { return m_pVideoStream; };
  AVStream *GetAudioStream() { return m_pAudioStream; };
  bool SetAudioStream(unsigned int index);
  void FreePacket(OMXPacket *pkt);
  int  GetChapterCount() { return m_chapter_count; };
  OMXChapter GetChapter(unsigned int chapter) { return m_chapters[(chapter > MAX_OMX_CHAPTERS) ? MAX_OMX_CHAPTERS : chapter]; };
  OMXPacket *AllocPacket(int size);
  void SetSpeed(int iSpeed);
  void UpdateCurrentPTS();
  double ConvertTimestamp(int64_t pts, int den, int num);
  double ConvertTimestamp(int64_t pts, AVRational *time_base);
  int GetChapter();
  void GetChapterName(std::string& strChapterName);
  bool SeekChapter(int chapter, double* startpts);
  bool GetAudioIndex() { return m_audio_index; };
  int GetStreamLength();
  static double NormalizeFrameduration(double frameduration);
  bool IsMpegVideo() { return m_bMpeg; };
  bool IsMatroska() { return m_bMatroska; };
  CStdString GetAudioCodecName();
  CStdString GetVideoCodecName();
  CStdString GetAudioCodecName(int index);
  CStdString GetVideoCodecName(int index);
  void GetStreamCodecName(AVStream *stream, CStdString &strStreamName);
  bool GetAudioStreamLanguage(int iStream, CStdString &strLanguage);
  int64_t GetDuration() { return m_duration_ms; };
#ifndef STANDALONE
  int GetSourceBitrate();
#endif
};
#endif
