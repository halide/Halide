#include "io/FileReader.h"

#include <cstdio>

// static
bool FileReader::readTextFile( const char* filename, char** ppBuffer, long* length )
{
	FILE* filePointer = fopen( filename, "r" );
	bool succeeded = false;
	if( filePointer != NULL )
	{
		// get the file size
		int fSeekResult = fseek( filePointer, 0L, SEEK_END );
		if( fSeekResult == 0 )
		{
			long fileSize = ftell( filePointer );
			char* buffer = new char[ fileSize + 1 ];
			if( buffer != NULL )
			{
				fseek( filePointer, 0L, SEEK_SET );
				*length = ( long )( fread( buffer, 1, fileSize, filePointer ) ); // 1 = 1 byte = item size

				succeeded = true;
				buffer[ *length ] = '\0';
				*ppBuffer = buffer;				
			}						
		}
		fclose( filePointer );
	}

	return succeeded;
}

// static
bool FileReader::readBinaryFile( const char* filename, ubyte** ppBuffer, long* size )
{
	FILE* filePointer = fopen( filename, "rb" );
	bool succeeded = false;
	if( filePointer != NULL )
	{
		// get the file size
		int fSeekResult = fseek( filePointer, 0L, SEEK_END );
		if( fSeekResult == 0 )
		{
			long fileSize = ftell( filePointer );
			ubyte* buffer = new ubyte[ fileSize  ];
			if( buffer != NULL )
			{
				fseek( filePointer, 0L, SEEK_SET );
				*size = ( long )( fread( buffer, 1, fileSize, filePointer ) ); // 1 = 1 byte = item size
				succeeded = true;
				*ppBuffer = buffer;
			}						
		}
		fclose( filePointer );
	}

	return succeeded;
}
