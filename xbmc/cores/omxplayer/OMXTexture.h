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

#include <IL/OMX_Video.h>

#include "OMXClock.h"
#include "OMXImage.h"
#include "filesystem/File.h"

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
  int  Decode(COMXImage *omx_image, unsigned width, unsigned height);
  void Reset(void);
  OMX_BUFFERHEADERTYPE *GetOutputBuffer() { return m_output_buffer; };
  int GetWidth() { return (int)m_output_format.format.image.nFrameWidth; };
  int GetHeight() { return (int)m_output_format.format.image.nFrameHeight; };
  int GetStride() { return (int)m_output_format.format.image.nStride; };
  unsigned char *GetData();
  unsigned int GetSize();
protected:

  // Components
  COMXCoreComponent             m_omx_decoder;
  COMXCoreComponent             m_omx_resize;
  COMXCoreTunel                 m_omx_tunnel_decode;
  OMX_BUFFERHEADERTYPE          *m_output_buffer;
  OMX_PARAM_PORTDEFINITIONTYPE  m_output_format;

  bool              m_is_open;
};

#endif
