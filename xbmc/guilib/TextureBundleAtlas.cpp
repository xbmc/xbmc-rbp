/*
 *      Copyright (C) 2005-2009 Team XBMC
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

#include "libsquish/squish.h"
#include "system.h"
#include "TextureBundleAtlas.h"
#include "Texture.h"
#include "GraphicContext.h"
#include "utils/log.h"
#include "addons/Skin.h"
#include "settings/GUISettings.h"
#include "filesystem/SpecialProtocol.h"
#include "utils/EndianSwap.h"
#include "utils/URIUtils.h"
#include "XBTF.h"
#include "pictures/DllImageLib.h"
#include <lzo/lzo1x.h>
#include "windowing/WindowingFactory.h"

#ifdef _WIN32
#pragma comment(lib,"liblzo2.lib")
#endif

CTextureBundleAtlas::CTextureBundleAtlas(void)
{
  m_themeBundle = false;
}

CTextureBundleAtlas::~CTextureBundleAtlas(void)
{
  Cleanup();
}

bool CTextureBundleAtlas::OpenBundle()
{
//  Cleanup();

  // Find the correct texture file (skin or theme)
  CStdString strIndex;

  if (m_themeBundle)
  {
    // if we are the theme bundle, we only load if the user has chosen
    // a valid theme (or the skin has a default one)
    CStdString theme = g_guiSettings.GetString("lookandfeel.skintheme");
    if (!theme.IsEmpty() && theme.CompareNoCase("SKINDEFAULT"))
    {
      CStdString themeAtlas(URIUtils::ReplaceExtension(theme, ".xml"));
      strIndex = URIUtils::AddFileToFolder(g_graphicsContext.GetMediaDir(), "media");
      strIndex = URIUtils::AddFileToFolder(strIndex, themeAtlas);
    }
    else
    {
      return false;
    }
  }
  else
  {
    strIndex = URIUtils::AddFileToFolder(g_graphicsContext.GetMediaDir(), "media");
    strIndex = URIUtils::AddFileToFolder(strIndex, "Textures.xml");
  }

  strIndex = CSpecialProtocol::TranslatePathConvertCase(strIndex);

  if (!m_atlasReader.Open(strIndex))
  {
    return false;
  }

  m_TimeStamp = m_atlasReader.GetLastModificationTimestamp();
  CLog::Log(LOGDEBUG, "%s - Opened bundle %s", __FUNCTION__, strIndex.c_str());

  return true;
}

bool CTextureBundleAtlas::HasFile(const CStdString& Filename)
{
  if (!m_atlasReader.IsOpen() && !OpenBundle())
    return false;

  if (m_atlasReader.GetLastModificationTimestamp() > m_TimeStamp)
  {
    CLog::Log(LOGINFO, "Texture bundle has changed, reloading");
    if (!OpenBundle())
      return false;
  }

  CStdString name = Normalize(Filename);
  return m_atlasReader.Exists(name);
}

void CTextureBundleAtlas::GetTexturesFromPath(const CStdString &path, std::vector<CStdString> &textures)
{
  if (path.GetLength() > 1 && path[1] == ':')
    return;

  if (!m_atlasReader.IsOpen() && !OpenBundle())
    return;

  CStdString testPath = Normalize(path);
  URIUtils::AddSlashAtEnd(testPath);
  int testLength = testPath.GetLength();

  std::vector<CXBTFFile>& files = m_atlasReader.GetFiles();
  for (size_t i = 0; i < files.size(); i++)
  {
    CStdString path = files[i].GetPath();
    if (path.Left(testLength).Equals(testPath))
      textures.push_back(path);
  }
}

bool CTextureBundleAtlas::LoadTexture(const CStdString& Filename, CBaseTexture** ppTexture,
                                     int &width, int &height)
{

  if (!m_atlasReader.IsOpen() && !OpenBundle())
    return false;

  CStdString name = Normalize(Filename);

  CXBTFFile* file = m_atlasReader.Find(name);
  if (!file)
    return false;

  if (file->GetFrames().size() == 0)
    return false;

  CXBTFFrame& frame = file->GetFrames().at(0);
  if (!ConvertFrameToTexture(file->GetAtlas().c_str(), Filename, frame, ppTexture))
  {
    return false;
  }
  width = frame.GetWidth();
  height = frame.GetHeight();

  return true;
}

bool CTextureBundleAtlas::ConvertFrameToTexture(const CStdString &atlas, const CStdString& name, CXBTFFrame& frame, CBaseTexture** ppTexture)
{
  // create an xbmc texture
  *ppTexture = new CTexture(frame.GetWidth(), frame.GetHeight(), XB_FMT_A8R8G8B8, false);

  CStdString strAtlas = "media/" + atlas;

  strAtlas = URIUtils::AddFileToFolder(g_graphicsContext.GetMediaDir(), strAtlas);

  CTexture *pAtlas = NULL;

  if(m_atlasTexture[strAtlas] == 0)
  {
    pAtlas = new CTexture(frame.GetAtlasWidth(), frame.GetAtlasHeight(), XB_FMT_A8R8G8B8, false);
    if(!pAtlas)
      return false;

    if (pAtlas->LoadFromFile((const CStdString)strAtlas))
    {
      pAtlas->LoadToGPU();
      m_atlasTexture[strAtlas] = pAtlas;
    }
    else
    {
      return false;
    }
  }

  pAtlas = m_atlasTexture[strAtlas];

  (*ppTexture)->LoadFromAtlas(pAtlas->GetTextureObject(), frame.GetAtlasWidth(), frame.GetAtlasHeight(), 
                              frame.GetTextureXOffset(), frame.GetTextureYOffset(), 
                              frame.HasAlpha());

  return true;
}

void CTextureBundleAtlas::SetThemeBundle(bool themeBundle)
{
  m_themeBundle = themeBundle;
}

void CTextureBundleAtlas::Cleanup()
{
  if (m_atlasReader.IsOpen())
  {
    m_atlasReader.Close();
    CLog::Log(LOGDEBUG, "%s - Closed %sbundle", __FUNCTION__, m_themeBundle ? "theme " : "");
  }

  for(AtlasTexture::iterator it = m_atlasTexture.begin(); it != m_atlasTexture.end(); it++)
  {
    CTexture *pTexture = (*it).second;
    delete pTexture;
  }
  m_atlasTexture.clear();
}

// normalize to how it's stored within the bundle
// lower case + using forward slash rather than back slash
CStdString CTextureBundleAtlas::Normalize(const CStdString &name)
{
  CStdString newName(name);
  newName.Normalize();
  newName.Replace('\\','/');

  return newName;
}

