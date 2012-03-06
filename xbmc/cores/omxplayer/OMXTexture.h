#pragma once
/*
 *      Copyright (C) 2010 Team XBMC
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

#if defined(HAVE_OMXLIB)

#include "OMXCore.h"
#include "OMXStreamInfo.h"

#include <IL/OMX_Video.h>

#include "OMXClock.h"
#include "OMXImage.h"
#ifdef STANDALONE
#include "File.h"
#else
#include "xbmc/filesystem/File.h"
#endif

using namespace XFILE;
using namespace std;

class DllAvUtil;
class DllAvFormat;
class COMXTexture
{
public:
  COMXTexture();
  virtual ~COMXTexture();

  // Required overrides
  bool Open(void);
  void Close(void);
  int  Decode(COMXImage *omx_image, void *egl_image, void *egl_display, unsigned width, unsigned height);
  void Reset(void);
protected:

  // Components
  COMXCoreComponent m_omx_image_decode;
  COMXCoreComponent m_omx_resize;
  COMXCoreComponent m_omx_egl_render;

  COMXCoreTunel     m_omx_tunnel_decode;
  COMXCoreTunel     m_omx_tunnel_egl;

  OMX_BUFFERHEADERTYPE *m_egl_buffer;
  bool              m_is_open;
};

#endif
