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

#if defined(HAVE_LIBOPENMAX)

#include "OMXCore.h"
#include "OMXStreamInfo.h"

#if (HAVE_LIBOPENMAX == 2)
#include <IL/OMX_Video.h>
#else
#include <OMX_Core.h>
#include <OMX_Component.h>
#include <OMX_Index.h>
#include <OMX_Image.h>
#endif

#include "OMXClock.h"
#include "xbmc/filesystem/File.h"

//#define CLASSNAME "COMXTexture"
//typedef void *EGLImageKHR;

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
  bool ReadFile(const std::string &inputFile);
  OMX_IMAGE_CODINGTYPE GetCodingType();
  bool IsProgressive() { return m_progressive; };
  int  Decode(void *egl_image, void *egl_display, unsigned width, unsigned height);
  OMX_U32 GetOriginalWidth()  { return m_omx_image.nFrameWidth; };
  OMX_U32 GetOriginalHeight() { return m_omx_image.nFrameHeight; };
  OMX_U32 GetWidth()  { return m_width; };
  OMX_U32 GetHeight() { return m_height; };
  void Reset(void);
protected:
  bool SetImageAutodetect(void);

  // Components
  COMXCoreComponent m_omx_image_decode;
  COMXCoreComponent m_omx_resize;
  COMXCoreComponent m_omx_egl_render;

  COMXCoreTunel     m_omx_tunnel_decode;
  COMXCoreTunel     m_omx_tunnel_egl;

  OMX_BUFFERHEADERTYPE *m_egl_buffer;
  bool              m_is_open;
  uint8_t           *m_image_buffer;
  unsigned long     m_image_size;
  OMX_IMAGE_PORTDEFINITIONTYPE m_omx_image;
  unsigned int      m_width;
  unsigned int      m_height;
  bool              m_progressive;
  COMXCore          m_OMX;
  XFILE::CFile      m_pFile;
};

#endif
