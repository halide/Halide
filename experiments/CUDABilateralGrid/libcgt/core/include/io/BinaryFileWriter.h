#ifndef BINARY_FILE_WRITER_H
#define BINARY_FILE_WRITER_H

#include <cstdio>

// TODO: binary file output stream
class BinaryFileWriter
{
public:

	static BinaryFileWriter* open( const char* filename );
	virtual ~BinaryFileWriter();

	void close();

	bool writeInt( int i );
	bool writeFloat( float f );
	bool writeFloatArray( float f[], int nCount );

private:

	BinaryFileWriter( FILE* pFilePointer );

	FILE* m_pFilePointer;
};

#endif BINARY_FILE_WRITER_H
