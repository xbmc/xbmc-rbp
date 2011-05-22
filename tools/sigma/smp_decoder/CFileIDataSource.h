#pragma once
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

#include <stdint.h>
#include <stdio.h>

class CFileIDataSource
{
public:
  struct SChannelParams
  {
    uint32_t  blockSize;
    uint32_t  bitrate;
    uint32_t  delay;
    uint8_t   nonBlocking;
  };

  /// Data source operation result code
  enum EDSResult
  {
    DSRES_OK = 0,           // operation completed successfully
    DSRES_INVALID_PARAMS,   // operation failed because of invalid parameters (wrong URL, seek position, etc)
    DSRES_SOURCE_MISSING,   // operation failed because the source is currently unavailable (disc ejected, connection closed, etc)
    DSRES_SOURCE_TIMEOUT,   // operation has not completed inside the accepted time interval, but it may be a temporary situation
    DSRES_SOURCE_ERROR,     // operation failed due to an internal error reported by the source
    DSRES_SOURCE_PROTECTED, // operation failed because the source is protected
    DSRES_SOURCE_NOSUPPORT, // operation failed because the source does not support it
    DSRES_ERROR,            // operation failed due to an unspecified error (either invalid parameters or source problems)
    DSRES_EOF,              // end of file has been encountered
  };

  CFileIDataSource() {};
  virtual ~CFileIDataSource() {};

  virtual void* GetFormatSpecificCPInterface();

  // Returns channel parameters like block size, bitrate, delay.
  virtual bool GetChannelParams(struct SChannelParams *pParams);

  // Opens a data channel for reading
  // Returns an opaque channel handle.
  // The url of the data is transparent to the caller
  virtual void* Open(char* url, enum EDSResult *pRes = NULL);

  // Reads size bytes from the current location of the specified, previously opened channel.
  // Blocks until the requested size has been read or an exception occurs
  // Returns the number of bytes actually read, or:
  //   0, after the end of source has been reached
  //  -1, if any error has occurred (the optional parameter pRes holds, if present, the error reason)
  virtual uint32_t Read(void *ch, uint32_t size, unsigned char *buf, enum EDSResult *pRes = NULL);

  // Seeks at the specified location of the specified, previously opened channel.
  // The seek can be absolute (from start) or relative (to the current position).
  // For seeking at the end of the file, specify -1 as absolute position
  // Returns the new absolute position into the file if successful, -1 otherwise.
  // All data sources that don't support seeking must return -1 for every position,
  // especially for relative position 0 (which is used in order to determine whether
  // the data source supports seeking).
  virtual int64_t Seek(void* ch, int64_t pos, bool isRel = 0, enum EDSResult *pRes = NULL);

  // Flushes the internal or the external portion of the channel
  // internal: Flushing the channel unblocks the pending Read() call, if any,
  //  that has been waiting for data to become available.
  // external: Flushing the channel discards any data not read yet.
  //
  // The expected use is: Flush(internal), (temporarily) stop the reading process, Flush(external)
  virtual void Flush(void *ch, bool internal = false) {};

  // Closes a previously opened channel.
  virtual void Close(void* ch);
  
private:
  int     m_fp;
};
