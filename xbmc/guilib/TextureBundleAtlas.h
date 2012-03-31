#pragma once

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

#include "utils/StdString.h"
#include <map>
#include "pictures/DllImageLib.h"
#include "XBTF.h"
#include "XBTFReader.h"
#include "AtlasReader.h"
#include "Texture.h"

#include <map>

class CBaseTexture;

class CTextureBundleAtlas
{
public:
  CTextureBundleAtlas(void);
  ~CTextureBundleAtlas(void);

  void Cleanup();
  bool HasFile(const CStdString& Filename);
  void GetTexturesFromPath(const CStdString &path, std::vector<CStdString> &textures);
  static CStdString Normalize(const CStdString &name);

  bool LoadTexture(const CStdString& Filename, CBaseTexture** ppTexture,
                       int &width, int &height);
  void SetThemeBundle(bool themeBundle);
private:
  bool OpenBundle();
  bool ConvertFrameToTexture(const CStdString &atlas, const CStdString& name, CXBTFFrame& frame, CBaseTexture** ppTexture);

  time_t m_TimeStamp;

  bool m_themeBundle;
  ImageInfo image;
  char *idxBuf;
  int atlasWidth;
  int atlasHeight;
  CAtlasReader m_atlasReader;
  unsigned char * m_pixels;

  typedef std::map<CStdString, CTexture *> AtlasTexture;
  AtlasTexture m_atlasTexture;
};

