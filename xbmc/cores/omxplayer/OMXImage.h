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
#include "xbmc/filesystem/File.h"

using namespace XFILE;
using namespace std;

class COMXImage
{
public:
  COMXImage();
  virtual ~COMXImage();

  // Required overrides
  void Close(void);
  bool ReadFile(const std::string &inputFile);
  bool IsProgressive() { return m_progressive; };
  bool IsAlpha() { return m_alpha; };
  int  GetOrientation() { return m_orientation; };
  OMX_U32 GetOriginalWidth()  { return m_omx_image.nFrameWidth; };
  OMX_U32 GetOriginalHeight() { return m_omx_image.nFrameHeight; };
  OMX_U32 GetWidth()  { return m_width; };
  OMX_U32 GetHeight() { return m_height; };
  OMX_IMAGE_CODINGTYPE GetCodingType();
  const uint8_t *GetImageBuffer() { return (const uint8_t *)m_image_buffer; };
  unsigned long GetImageSize() { return m_image_size; };
  OMX_IMAGE_CODINGTYPE GetCompressionFormat() { return m_omx_image.eCompressionFormat; };
protected:
  uint8_t           *m_image_buffer;
  bool              m_is_open;
  unsigned long     m_image_size;
  unsigned int      m_width;
  unsigned int      m_height;
  bool              m_progressive;
  bool              m_alpha;
  int               m_orientation;
  XFILE::CFile      m_pFile;
  OMX_IMAGE_PORTDEFINITIONTYPE m_omx_image;
};

#endif
