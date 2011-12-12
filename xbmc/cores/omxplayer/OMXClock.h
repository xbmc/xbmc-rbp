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

#ifndef _AVCLOCK_H_
#define _AVCLOCK_H_

#include "DllAvFormat.h"

#include "OMXCore.h"

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0
#define SAMPLE_CORRECTION_PERCENT_MAX 10
#define AUDIO_DIFF_AVG_NB 20

#define DVD_TIME_BASE 1000000
#define DVD_NOPTS_VALUE    (-1LL<<52) // should be possible to represent in both double and __int64

#define DVD_TIME_TO_SEC(x)  ((int)((double)(x) / DVD_TIME_BASE))
#define DVD_TIME_TO_MSEC(x) ((int)((double)(x) * 1000 / DVD_TIME_BASE))
#define DVD_SEC_TO_TIME(x)  ((double)(x) * DVD_TIME_BASE)
#define DVD_MSEC_TO_TIME(x) ((double)(x) * DVD_TIME_BASE / 1000)

#define DVD_PLAYSPEED_PAUSE       0       // frame stepping
#define DVD_PLAYSPEED_NORMAL      1000

enum {
  AV_SYNC_AUDIO_MASTER,
  AV_SYNC_VIDEO_MASTER,
  AV_SYNC_EXTERNAL_MASTER,
};

class OMXClock
{
protected:
  double m_video_clock;
  double m_audio_clock;
  bool   m_pause;
  double m_iCurrentPts;
  bool   m_has_video;
  bool   m_has_audio;
private:
  COMXCoreComponent m_omx_clock;
  DllAvFormat       m_dllAvFormat;
public:
  OMXClock();
  ~OMXClock();
  bool Reset();
  bool Initialize(bool has_video, bool has_audio);
  void Deinitialize();
  bool IsPaused() { return m_pause; };
  bool Pause();
  bool Resume();
  bool WaitStart(uint64_t pts);
  bool Speed(int speed);
  COMXCoreComponent *GetOMXClock();
  bool StateExecute();
  static void Sleep(unsigned int dwMilliSeconds);
  static double NormalizeFrameduration(double frameduration);
  static double ConvertTimestamp(int64_t pts, int64_t start_time, AVRational *time_base);
  void UpdateCurrentPTS(AVFormatContext *ctx);
  double GetCurrentPts() { return m_iCurrentPts; };
  void SetCurrentPts(int64_t pts) { m_iCurrentPts = pts; };
  void UpdateVideoClock(double videoClock) { m_video_clock = videoClock; };
  double GetVideoClock() { return m_video_clock; };
  void UpdateAudioClock(double audioClock) { m_audio_clock = audioClock; };
  double GetAudioClock() { return m_audio_clock; };
};

inline void OMXSleep(unsigned int dwMilliSeconds)
{
  struct timespec req;
  req.tv_sec = dwMilliSeconds / 1000;
  req.tv_nsec = (dwMilliSeconds % 1000) * 1000000;

  while ( nanosleep(&req, &req) == -1 && errno == EINTR && (req.tv_nsec > 0 || req.tv_sec > 0));
  //usleep(dwMilliSeconds * 1000);
}

#endif
