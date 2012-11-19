#ifndef FILE_READER_H
#define FILE_READER_H

#include "common/BasicTypes.h"

class FileReader
{
public:
	
	// allocates a new buffer and points *ppBuffer to it
	// buffer contains *length characters followed by '\0'
	// the buffer size is 1 + fileSize bytes
	// note that length may be less than fileSize due to fread() converting "\r\n" to "\n"
	// returns true if succeeded
	// if failed, buffer is correctly deallocated
	static bool readTextFile( const char* filename, char** ppBuffer, long* length );

	static bool readBinaryFile( const char* filename, ubyte** ppBuffer, long* size );
};

#endif
