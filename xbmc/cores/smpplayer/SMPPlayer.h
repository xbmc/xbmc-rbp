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

typedef struct _IDirectFBEventBuffer IDirectFBEventBuffer;
typedef struct _IAdvancedMediaProvider IAdvancedMediaProvider;

class CSMPPlayer : public IPlayer, public CThread
{
public:

  CSMPPlayer(IPlayerCallback &callback);
  virtual ~CSMPPlayer();
  
  virtual void  RegisterAudioCallback(IAudioCallback* pCallback) {}
  virtual void  UnRegisterAudioCallback()                        {}
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
  virtual void  SetVolume(long nVolume);
  virtual void  SetDynamicRangeCompression(long drc)              {}
  virtual void  GetAudioInfo(CStdString &strAudioInfo);
  virtual void  GetVideoInfo(CStdString &strVideoInfo);
  virtual void  GetGeneralInfo(CStdString &strVideoInfo) {};
  virtual void  Update(bool bPauseDrawing);
  virtual void  GetVideoRect(CRect& SrcRect, CRect& DestRect);
  virtual void  GetVideoAspectRatio(float &fAR);
  virtual bool  CanRecord()                                       {return false;};
  virtual bool  IsRecording()                                     {return false;};
  virtual bool  Record(bool bOnOff)                               {return false;};

  virtual void  SetAVDelay(float fValue = 0.0f)                   {return;}
  virtual float GetAVDelay()                                      {return 0.0f;};

  virtual void  SetSubTitleDelay(float fValue = 0.0f)             {};
  virtual float GetSubTitleDelay()                                {return 0.0f;}
  virtual int   GetSubtitleCount();
  virtual int   GetSubtitle();
  virtual void  GetSubtitleName(int iStream, CStdString &strStreamName);
  virtual void  SetSubtitle(int iStream);
  virtual bool  GetSubtitleVisible()                              {return false;};
  virtual void  SetSubtitleVisible(bool bVisible)                 {};
  virtual bool  GetSubtitleExtension(CStdString &strSubtitleExtension){return false;};
  virtual int   AddSubtitle(const CStdString& strSubPath)         {return -1;};

  virtual int   GetAudioStreamCount();
  virtual int   GetAudioStream();
  virtual void  GetAudioStreamName(int iStream, CStdString &strStreamName);
  virtual void  SetAudioStream(int iStream);
  virtual void  GetAudioStreamLanguage(int iStream, CStdString &strLanguage) {};

  virtual TextCacheStruct_t* GetTeletextCache()                   {return NULL;};
  virtual void  LoadPage(int p, int sp, unsigned char* buffer)    {};

  virtual int   GetChapterCount()                                 {return 0;}
  virtual int   GetChapter()                                      {return -1;}
  virtual void  GetChapterName(CStdString& strChapterName)        {return; }
  virtual int   SeekChapter(int iChapter)                         {return -1;}

  virtual float GetActualFPS()                                    {return 0.0f;};
  virtual void  SeekTime(__int64 iTime = 0);
  virtual __int64 GetTime();
  virtual int   GetTotalTime();
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
  virtual void  ToFFRW(int iSpeed = 0);
  // Skip to next track/item inside the current media (if supported).
  virtual bool  SkipNext()                                        {return false;}

  //Returns true if not playback (paused or stopped beeing filled)
  virtual bool  IsCaching() const                                 {return false;};
  //Cache filled in Percent
  virtual int   GetCacheLevel() const                             {return -1;};

  virtual bool  IsInMenu() const                                  {return false;};
  virtual bool  HasMenu()                                         {return false;};

  virtual void  DoAudioWork()                                     {};
  virtual bool  OnAction(const CAction &action)                   {return false;};

  virtual bool  GetCurrentSubtitle(CStdString& strSubtitle)       {strSubtitle = ""; return false;}
  //returns a state that is needed for resuming from a specific time
  virtual CStdString GetPlayerState()                             {return "";};
  virtual bool  SetPlayerState(CStdString state)                  {return false;};
  
  virtual CStdString GetPlayingTitle()                            {return "";};
  
protected:
  virtual void  OnStartup();
  virtual void  OnExit();
  virtual void  Process();
  
private:
  bool          WaitForAmpPlaying(int timeout);

  int                     m_speed;
  bool                    m_paused;
  bool                    m_StopPlaying;
  CEvent                  m_ready;
  CFileItem               m_item;
  CPlayerOptions          m_options;
  CCriticalSection        m_StateSection;
  
  IAdvancedMediaProvider  *m_amp;
  IDirectFBEventBuffer    *m_amp_event;
  int                     m_ampID;

  // opaque smp global status
  void*                   m_status;

};
