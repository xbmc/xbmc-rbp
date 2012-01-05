#pragma once
/*
 *      Copyright (C) 2011 Team XBMC
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

#include "FileItem.h"
#include "cores/IPlayer.h"
#include "dialogs/GUIDialogBusy.h"
#include "threads/Thread.h"
#include "OMXStreamInfo.h"
#include <semaphore.h>

#include "DllAvUtil.h"
#include "DllAvFormat.h"
#include "DllAvFilter.h"
#include "DllAvCodec.h"
#include "DllAvCore.h"

#include "threads/SingleLock.h"

#include "utils/PCMRemap.h"

#include "OMXAudioCodecOMX.h"
#include "OMXCore.h"
#include "OMXClock.h"
#include "OMXVideo.h"
#include "OMXAudio.h"

#include "utils/BitstreamStats.h"

// TODO: remove after we have it in configure
#ifndef HAVE_LIBBCM_HOST
#define HAVE_LIBBCM_HOST
#endif

#include "linux/DllBCM.h"

#define MAX_CHAPTERS 64

typedef struct Chapter
{
  std::string name;
  int64_t     seekto_ms;
  double      ts;
} Chapter;

class COMXPlayer : public IPlayer, public CThread
{
public:

  COMXPlayer(IPlayerCallback &callback);
  virtual ~COMXPlayer();
  
  virtual bool  Initialize(TiXmlElement* pConfig);
  virtual void  RegisterAudioCallback(IAudioCallback* pCallback);
  virtual void  UnRegisterAudioCallback();
  virtual bool  OpenFile(const CFileItem &file, const CPlayerOptions &options);
  virtual bool  QueueNextFile(const CFileItem &file)             {return false;}
  virtual void  OnNothingToQueueNotify()                         {}
  virtual bool  CloseFile();
  virtual bool  IsPlaying() const;
  virtual void  Pause();
  virtual bool  IsPaused() const;
  virtual bool  HasVideo() const;
  virtual bool  HasAudio() const;
  virtual void  ToggleFrameDrop();
  virtual bool  CanSeek();
  virtual void  Seek(bool bPlus = true, bool bLargeStep = false);
  virtual bool  SeekScene(bool bPlus = true);
  virtual void  SeekPercentage(float fPercent = 0.0f);
  virtual float GetPercentage();
  virtual float GetCachePercentage();

  virtual void  SetVolume(long nVolume);
  virtual void  SetDynamicRangeCompression(long drc)              {}
  virtual void  GetAudioInfo(CStdString &strAudioInfo);
  virtual void  GetVideoInfo(CStdString &strVideoInfo);
  virtual void  GetGeneralInfo(CStdString &strVideoInfo);
  virtual void  Update(bool bPauseDrawing);
  virtual void  GetVideoRect(CRect& SrcRect, CRect& DestRect);
  virtual void  SetVideoRect(const CRect &SrcRect, const CRect &DestRect);
  virtual void  GetVideoAspectRatio(float &fAR);
  virtual bool  CanRecord()                                       {return false;};
  virtual bool  IsRecording()                                     {return false;};
  virtual bool  Record(bool bOnOff)                               {return false;};
  virtual void  SetAVDelay(float fValue = 0.0f);
  virtual float GetAVDelay();

  virtual void  SetSubTitleDelay(float fValue = 0.0f);
  virtual float GetSubTitleDelay();
  virtual int   GetSubtitleCount();
  virtual int   GetSubtitle();
  virtual void  GetSubtitleName(int iStream, CStdString &strStreamName);
  virtual void  SetSubtitle(int iStream);
  virtual bool  GetSubtitleVisible();
  virtual void  SetSubtitleVisible(bool bVisible);
  virtual bool  GetSubtitleExtension(CStdString &strSubtitleExtension) { return false; }
  virtual int   AddSubtitle(const CStdString& strSubPath);

  virtual int   GetAudioStreamCount();
  virtual int   GetAudioStream();
  virtual void  GetAudioStreamName(int iStream, CStdString &strStreamName);
  virtual void  SetAudioStream(int iStream);
  virtual void  GetAudioStreamLanguage(int iStream, CStdString &strLanguage);

  virtual TextCacheStruct_t* GetTeletextCache()                   {return NULL;};
  virtual void  LoadPage(int p, int sp, unsigned char* buffer)    {};

  virtual int   GetChapterCount();
  virtual int   GetChapter();
  virtual void  GetChapterName(CStdString& strChapterName);
  virtual int   SeekChapter(int iChapter);

  virtual float GetActualFPS();
  virtual void  SeekTime(__int64 iTime = 0);
  virtual __int64 GetTime();
  virtual int   GetTotalTime();
  virtual void  ToFFRW(int iSpeed = 0);
  virtual int   GetAudioBitrate();
  virtual int   GetVideoBitrate();
  virtual int   GetSourceBitrate();
  virtual int   GetChannels();
  virtual int   GetBitsPerSample();
  virtual int   GetSampleRate();
  virtual CStdString GetAudioCodecName();
  virtual CStdString GetVideoCodecName();
  virtual int   GetPictureWidth();
  virtual int   GetPictureHeight();
  virtual bool  GetStreamDetails(CStreamDetails &details);
  // Skip to next track/item inside the current media (if supported).
  virtual bool  SkipNext()                                        {return false;}

  virtual bool  IsInMenu() const                                  {return false;};
  virtual bool  HasMenu()                                         {return false;};

  virtual void  DoAudioWork();
  virtual bool  OnAction(const CAction &action)                   {return false;};

  virtual bool  GetCurrentSubtitle(CStdString& strSubtitle);
  //returns a state that is needed for resuming from a specific time
  virtual CStdString GetPlayerState();
  virtual bool  SetPlayerState(CStdString state);
  
  virtual CStdString GetPlayingTitle();
  
  virtual bool  IsCaching() const                                 {return false;};
  virtual int   GetCacheLevel() const;

protected:
  virtual void  OnStartup();
  virtual void  OnExit();
  virtual void  Process();

  std::string m_filename; // holds the actual filename

private:

  std::vector<AVStream*> m_video_streams;
  std::vector<AVStream*> m_audio_streams;
 
  virtual bool GetStreams();
  virtual bool GetHints(AVStream *stream, COMXStreamInfo *hints);
  virtual bool OpenVideoDecoder(AVStream *stream);
  virtual void CloseVideoDecoder();
  virtual bool IsPassthrough(AVStream *stream);
  virtual bool OpenAudioCodec(AVStream *stream);
  virtual void CloseAudioCodec();
  virtual bool OpenAudioDecoder(AVStream *stream);
  virtual void CloseAudioDecoder();

  virtual void GetStreamCodecName(AVStream *stream, CStdString &strStreamName);

  int                     m_speed;
  bool                    m_paused;
  bool                    m_StopPlaying;
  CEvent                  m_ready;
  CFileItem               m_item;
  CPlayerOptions          m_options;

  CCriticalSection        m_csection;

  int64_t                 m_elapsed_ms;
  int64_t                 m_duration_ms;
  int                     m_audio_index;
  int                     m_audio_count;
  CStdString              m_audio_info;
  int64_t                 m_audio_offset_ms;
  int                     m_video_index;
  int                     m_video_count;
  CStdString              m_video_info;
  int                     m_subtitle_index;
  int                     m_subtitle_count;
  bool                    m_subtitle_show;
  int64_t                 m_subtitle_offset_ms;

  int                     m_chapter_count;

  Chapter                 m_chapters[MAX_CHAPTERS];

  float                   m_video_fps;
  int                     m_video_width;
  int                     m_video_height;
  CRect                   m_dst_rect;
  int                     m_view_mode;

  XFILE::CFile            *m_pFile;
  COMXStreamInfo          m_hints_audio;
  COMXStreamInfo          m_hints_video;
  AVFormatContext         *m_pFormatContext;
  ByteIOContext           *m_ioContext;
  DllAvUtil               m_dllAvUtil;
  DllAvCodec              m_dllAvCodec;
  DllAvFormat             m_dllAvFormat;
  DllBcmHostDisplay       m_BcmHostDisplay;
  DllBcmHost              m_BcmHost;

  CCriticalSection        m_SeekSection;
  int64_t                 m_seek_ms;
  int                     m_seek_req;

  AVStream                *m_pVideoStream;
  AVStream                *m_pAudioStream;
  bool                    m_AudioCodecOpen;
  bool                    m_VideoCodecOpen;
  bool                    m_AudioRenderOpen;
  OMXClock                *m_av_clock;
  COMXAudioCodecOMX       *m_pAudioCodec;
  COMXAudio               *m_audio_render;
  COMXVideo               *m_video_decoder;
  enum PCMChannels        *m_pChannelMap;

  CStdString              m_audio_codec_name;
  CStdString              m_video_codec_name;

  COMXCore                m_OMX;

  bool                    m_bMatroska;
  bool                    m_bAVI;
  bool                    m_bMpeg;
  double                  m_last_pts;
  double                  m_videoClock;
  double                  m_audioClock;
  double                  m_frametime;
  bool                    m_pkt_consumed;
  AVPacket                m_pkt;

  bool                    m_Passthrough;
  bool                    m_HWDecode;
  bool                    m_use_hw_audio;

  BitstreamStats          m_videoStats;
  TV_GET_STATE_RESP_T     m_tv_state;
  bool                    m_buffer_seek;
  bool                    m_mode3d_sbs;
};
