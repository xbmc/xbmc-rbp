/*
 *      Copyright (C) 2011 Team XBMC
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

#include "FileIDataSource.h"
#include "filesystem/File.h"
#include "FileItem.h"
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

//========================================================================
CFileIDataSource::CFileIDataSource(const char *url)
{
  strncpy(m_url, url, sizeof(m_url));
}

//========================================================================
CFileIDataSource::~CFileIDataSource()
{
}

//========================================================================
void* CFileIDataSource::GetFormatSpecificCPInterface()
{
  fprintf(stderr, "CFileIDataSource::GetFormatSpecificCPInterface\n");

  void *test = NULL;
  return test;
}

//========================================================================
void* CFileIDataSource::Open(char* url, enum EDSResult *pRes)
{
  // open the file, always in read mode, calc bitrate
  unsigned int flags = READ_BITRATE;
  XFILE::CFile *cfile = new XFILE::CFile();

  if (CFileItem(m_url, false).IsInternetStream())
    flags |= READ_CACHED;

  // open file in binary mode
  if (!cfile->Open(m_url, flags))
  {
    delete cfile;
    cfile = NULL;
  }

  fprintf(stderr, "CFileIDataSource::Open url(%s), filename(%s), ch(0x%08lx)\n",
    url, m_url, (long unsigned int)cfile);

  if (pRes)
  {
    if (cfile)
      *pRes = DSRES_OK;
    else
      *pRes = DSRES_SOURCE_MISSING;
  }

  return (void*)cfile;
}

//========================================================================
bool CFileIDataSource::GetChannelParams(struct SChannelParams *pParams)
{
  fprintf(stderr, "CFileIDataSource::GetChannelParams\n");
  // we do not know the channel parameters,
  /*
  pParams->blockSize;     // amount of data currently available in the buffer
  pParams->bitrate;
  pParams->delay;
  pParams->nonBlocking;   // whether the source is non-blocking or not
  */
return false;
}

//========================================================================
uint32_t CFileIDataSource::Read(void *ch, uint32_t size, unsigned char *buf, enum EDSResult *pRes)
{
  //fprintf(stderr, "CFileIDataSource::Read, ch(0x%08lx), size(%d)\n",
  //  (long unsigned int)ch, size);
  unsigned int readSize;
	XFILE::CFile *cfile = (XFILE::CFile*)ch;

  readSize = cfile->Read(buf, size);
  if (readSize > 0)
  {
    if (pRes)
      *pRes = DSRES_OK;
    return readSize;
  }
  else if (readSize == 0)
  {
    if (pRes)
      *pRes = DSRES_EOF;
    return 0;
  }
  else if (pRes)
    *pRes = DSRES_SOURCE_ERROR;

  return -1;
}

//========================================================================
int64_t CFileIDataSource::Seek(void *ch, int64_t pos, bool isRel, enum EDSResult *pRes)
{
  //fprintf(stderr, "CFileIDataSource::Seek, ch(0x%08lx), pos(%lld), isRel(%d)\n",
  //  (long unsigned int)ch, pos, isRel);
	XFILE::CFile *cfile = (XFILE::CFile*)ch;

  if (pRes)
    *pRes = DSRES_SOURCE_ERROR;

  if (!isRel && (pos == -1))
  {
    // seek to the end of file
    pos = cfile->Seek(0, SEEK_END);
    if (pos == -1)
      return -1;
  }
  else
  {
    pos = cfile->Seek(pos, isRel ? SEEK_CUR : SEEK_SET);
    if (pos == -1)
      return -1;
  }

  if (pRes)
    *pRes = DSRES_OK;

  return pos;
}

//========================================================================
void CFileIDataSource::Flush(void *ch, bool internal)
{
  fprintf(stderr, "CFileIDataSource::Flush, ch(0x%08lx), internal(%d)\n",
    (long unsigned int)ch, internal);
	XFILE::CFile *cfile = (XFILE::CFile*)ch;
  cfile->Flush();
};


//========================================================================
void CFileIDataSource::Close(void* ch)
{
  fprintf(stderr, "CFileIDataSource::Close, ch(0x%08lx)\n",
    (long unsigned int)ch);
	XFILE::CFile *cfile = (XFILE::CFile*)ch;
  cfile->Close();
  delete cfile;
}
