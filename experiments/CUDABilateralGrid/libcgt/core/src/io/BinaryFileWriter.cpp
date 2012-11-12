#include "io/BinaryFileWriter.h"

// ==============================================================
// Public
// ==============================================================

// static
BinaryFileWriter* BinaryFileWriter::open( const char* filename )
{
	FILE* fp = fopen( filename, "wb" );
	if( fp != NULL )
	{
		return new BinaryFileWriter( fp );
	}
	else
	{
		return NULL;
	}
}

// virtual
BinaryFileWriter::~BinaryFileWriter()
{
	close();
}

void BinaryFileWriter::close()
{
	fclose( m_pFilePointer );
}

bool BinaryFileWriter::writeInt( int i )
{
	size_t itemsWritten = fwrite( &i, sizeof( int ), 1, m_pFilePointer );
	return( itemsWritten == 1 );
}

bool BinaryFileWriter::writeFloat( float f )
{
	size_t itemsWritten = fwrite( &f, sizeof( float ), 1, m_pFilePointer );
	return( itemsWritten == 1 );
}

bool BinaryFileWriter::writeFloatArray( float f[], int nCount )
{
	size_t itemsWritten = fwrite( f, sizeof( float ), nCount, m_pFilePointer );
	return( itemsWritten == nCount );
}

// ==============================================================
// Private
// ==============================================================

BinaryFileWriter::BinaryFileWriter( FILE* pFilePointer ) :
	m_pFilePointer( pFilePointer )
{

}