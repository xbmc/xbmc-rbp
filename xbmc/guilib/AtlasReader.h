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

#ifndef ATLASREADER_H_
#define ATLASREADER_H_

#include <vector>
#include <map>
#include "utils/StdString.h"
#include "XBTF.h"
#include "filesystem/File.h"

#define ATLAS_WIDTH  2048
#define ATLAS_HEIGHT 2048

class TiXmlDocument;

class CAtlasReader
{
public:
  CAtlasReader();
  bool IsOpen() const;
  bool Open(const CStdString& fileName);
  void Close();
  time_t GetLastModificationTimestamp();
  bool Exists(const CStdString& name);
  CXBTFFile* Find(const CStdString& name);
  bool Load(const CXBTFFrame& frame, unsigned char* buffer);
  std::vector<CXBTFFile>&  GetFiles();
  CStdString GetFileName() { return m_fileName; };

private:
  CXBTF      m_atlas;
  CStdString m_fileName;
  XFILE::CFile      m_file;
  std::map<CStdString, CXBTFFile> m_filesMap;
  bool m_loaded;
  bool LoadXML(const CStdString &strFile);
};

#endif
