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

#include <sys/stat.h>
#include "AtlasReader.h"
#include "utils/EndianSwap.h"
#include "utils/CharsetConverter.h"
#include "utils/log.h"
#include "utils/URIUtils.h"
#ifdef _WIN32
#include "FileSystem/SpecialProtocol.h"
#endif

#include <string.h>
#include "PlatformDefs.h"

#include "filesystem/SpecialProtocol.h"
#include "GraphicContext.h"
#include "utils/XMLUtils.h"
#include "tinyXML/tinyxml.h"

CAtlasReader::CAtlasReader()
{
  m_loaded = false;
}

bool CAtlasReader::IsOpen() const
{
  return m_loaded;
}

bool CAtlasReader::LoadXML(CStdString strFile)
{
  TiXmlDocument xmlDoc;

  if(!xmlDoc.LoadFile(strFile))
  {
    CLog::Log(LOGERROR, "error load atlasmap file : %s\n", strFile.c_str());
    return false;
  }

  TiXmlElement* pRootElement = xmlDoc.RootElement();

  CStdString strValue = pRootElement->Value();
  if (strValue != CStdString("atlasmap"))
  {
    CLog::Log(LOGERROR, "atlasmap file doesnt start with <atlasmap>");
    return false;
  }

  const TiXmlElement *pChild = pRootElement->FirstChildElement("texture");
  while (pChild)
  {
    int x = 0, y = 0, width = 0, height = 0;

    CXBTFFile file;
    CXBTFFrame frame;

    CStdString strFileName;
    XMLUtils::GetString(pChild, "filename", strFileName);
    CStdString strTextureName;
    XMLUtils::GetString(pChild, "texturename", strTextureName);

    XMLUtils::GetInt(pChild, "x", x);
    XMLUtils::GetInt(pChild, "y", y);
    XMLUtils::GetInt(pChild, "width", width);
    XMLUtils::GetInt(pChild, "height", height);

    file.SetPath(strTextureName.ToLower());
    file.SetAtlas(strFileName);
    // the generator puts a border of 1 around the frame
    frame.SetWidth(width - 2);
    frame.SetHeight(height - 2);
    frame.SetTextureXOffset(x + 1);
    frame.SetTextureYOffset(y + 1);
    frame.SetAtlasWidth(ATLAS_WIDTH);
    frame.SetAtlasHeight(ATLAS_HEIGHT);

    file.GetFrames().push_back(frame);
    m_atlas.GetFiles().push_back(file);
    m_filesMap[file.GetPath()] = file;

    pChild = pChild->NextSiblingElement("texture");
  }

  return true;
}

bool CAtlasReader::Open(const CStdString& fileName)
{
  if(!LoadXML(fileName))
    return false;

  m_fileName = fileName;

  m_loaded = true;
  return true;
}

void CAtlasReader::Close()
{
  m_loaded = false;
  m_fileName = "";

  m_atlas.GetFiles().clear();
  m_filesMap.clear();
}

time_t CAtlasReader::GetLastModificationTimestamp()
{
  if (!XFILE::CFile::Exists(m_fileName))
  {
    return 0;
  }
  XFILE::CFile file;
  struct __stat64 buffer;
  if (XFILE::CFile::Stat(m_fileName,&buffer) == 0)
  {
    return buffer.st_mtime;
  }
  return 0;
}

bool CAtlasReader::Exists(const CStdString& name)
{
  return Find(name) != NULL;
}

CXBTFFile* CAtlasReader::Find(const CStdString& name)
{
  CStdString search = CStdString(name).ToLower();

  std::map<CStdString, CXBTFFile>::iterator iter = m_filesMap.find(search);
  if (iter == m_filesMap.end())
  {
    return NULL;
  }

  CXBTFFile *file = &(iter->second);
  CXBTFFrame& frame = file->GetFrames().at(0);

  CLog::Log(LOGDEBUG, "Loading Atlas image %s %d %d %d %d\n", name.c_str(), 
            frame.GetTextureXOffset(), frame.GetTextureYOffset(), 
            frame.GetWidth(), frame.GetHeight());

  return &(iter->second);
}

bool CAtlasReader::Load(const CXBTFFrame& frame, unsigned char* buffer)
{
  return true;
}

std::vector<CXBTFFile>& CAtlasReader::GetFiles()
{
  return m_atlas.GetFiles();
}

