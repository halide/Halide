#include "common/ArrayUtils.h"

#include <cstdlib>

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
ArrayWithLength< float > ArrayUtils::createFloatArray( int length, float fillValue )
{
	float* arr = new float[ length ];
	for( int i = 0; i < length; ++i )
	{
		arr[ i ] = fillValue;
	}
	
	return ArrayWithLength< float >( arr, length );
}

// static
bool ArrayUtils::saveTXT( const ArrayWithLength< float >& array, const char* filename )
{
	FILE* fp = fopen( filename, "w" );
	if( fp == NULL )
	{
		return false;
	}

	int retVal;

	retVal = fprintf( fp, "Size: %d\n", array.length() );
	if( retVal < 0 )
	{
		return false;
	}

	for( int i = 0; i < array.length(); ++i )
	{
		fprintf( fp, "[%d]: %f\n", i, array[ i ] );
	}
	
	retVal = fclose( fp );
	return( retVal == 0 );
}

// static
bool ArrayUtils::saveTXT( const Array2D< float >& array, const char* filename )
{
	FILE* fp = fopen( filename, "w" );
	if( fp == NULL )
	{
		return false;
	}
	
	int retVal;
	
	retVal = fprintf( fp, "Size: %d x %d\n", array.width(), array.height() );
	if( retVal < 0 )
	{
		return false;
	}

	retVal = fprintf( fp, "Format: float\n" );
	if( retVal < 0 )
	{
		return false;
	}

	for( int y = 0; y < array.height(); ++y )
	{
		for( int x = 0; x < array.width(); ++x )
		{
			float v = array( x, y );
			fprintf( fp, "[%d %d]: %f\n", x, y, v );
		}
	}
	
	retVal = fclose( fp );
	return( retVal == 0 );
}

// static
bool ArrayUtils::saveTXT( const Array3D< float >& array, const char* filename )
{
	FILE* fp = fopen( filename, "w" );
	if( fp == NULL )
	{
		return false;
	}

	int retVal;

	retVal = fprintf( fp, "Size: %d x %d x %d\n", array.width(), array.height(), array.depth() );
	if( retVal < 0 )
	{
		return false;
	}

	retVal = fprintf( fp, "Format: float2\n" );
	if( retVal < 0 )
	{
		return false;
	}

	for( int z = 0; z < array.depth(); ++z )
	{
		for( int y = 0; y < array.height(); ++y )
		{
			for( int x = 0; x < array.width(); ++x )
			{
				float v = array( x, y, z );
				fprintf( fp, "[%d %d %d]: %f\n", x, y, z, v );
			}
		}
	}
	
	retVal = fclose( fp );
	return( retVal == 0 );
}
