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

#pragma once
#include <list>
#include "threads/Thread.h"
#include "OMXCoreVideo.h"

#include "DVDStreamInfo.h"
#include "DllAvFormat.h"

#include "AVClock.h"

typedef struct stOMXVideoFrame
{
  uint8_t       *data;
  unsigned int  size;
  int64_t       pts;
  int64_t       dts;
} OMXVideoFrame;

class COMXPlayerVideo : public CThread
{
public:
  COMXPlayerVideo();
  virtual ~COMXPlayerVideo();

  void Flush();
  void Stop();
  bool Start(AVStream *pStream, CDVDStreamInfo &hints, AVClock *clock);
  void Process();

  unsigned int GetFreeSpace();
  unsigned int Add(const uint8_t *data, unsigned int size, int64_t dts, int64_t pts);

private:

  pthread_mutex_t   m_omx_input_mutex;

  typedef std::list<OMXVideoFrame *> VideoFrames;

  VideoFrames m_VideoFrames;

  CDVDStreamInfo m_hints;

  COMXCoreVideo *m_video_decoder;
  AVClock       *m_av_clock;
  AVStream      *m_pStream;
};
