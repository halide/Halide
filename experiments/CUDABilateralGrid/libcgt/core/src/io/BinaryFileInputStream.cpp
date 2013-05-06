#include "io/BinaryFileInputStream.h"

// ==============================================================
// Public
// ==============================================================

// static
BinaryFileInputStream* BinaryFileInputStream::open( const char* filename )
{
	FILE* fp = fopen( filename, "rb" );
	if( fp != NULL )
	{
		return new BinaryFileInputStream( fp );
	}
	else
	{
		return NULL;
	}
}

// virtual
BinaryFileInputStream::~BinaryFileInputStream()
{
	close();
}

void BinaryFileInputStream::close()
{
	fclose( m_pFilePointer );
}

bool BinaryFileInputStream::readInt( int* i )
{	
	size_t itemsRead = fread( i, sizeof( int ), 1, m_pFilePointer );
	return( itemsRead == 1 );
}

bool BinaryFileInputStream::readIntArray( int i[], int nCount )
{
	size_t itemsRead = fread( i, sizeof( int ), nCount, m_pFilePointer );
	return( itemsRead == nCount );
}

bool BinaryFileInputStream::readFloat( float* f )
{
	size_t itemsRead = fread( f, sizeof( float ), 1, m_pFilePointer );
	return( itemsRead == 1 );
}

bool BinaryFileInputStream::readFloatArray( float f[], int nCount )
{
	size_t itemsRead = fread( f, sizeof( float ), nCount, m_pFilePointer );
	return( itemsRead == nCount );
}

// ==============================================================
// Private
// ==============================================================

BinaryFileInputStream::BinaryFileInputStream( FILE* pFilePointer ) :
	m_pFilePointer( pFilePointer )
{

}
