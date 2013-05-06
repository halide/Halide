#include "ArrayUtils.h"

bool ArrayUtils::saveTXT( const Array2D< float2 >& array, const char* filename )
{
	FILE* fp = fopen( filename, "w" );
	if( fp == NULL )
	{
		return false;
	}

	fprintf( fp, "Size: %d x %d\n", array.width(), array.height() );
	fprintf( fp, "Format: float2\n" );

	for( int y = 0; y < array.height(); ++y )
	{
		for( int x = 0; x < array.width(); ++x )
		{
			float2 v = array( x, y );
			fprintf( fp, "[%d %d]: %f, %f\n", x, y, v.x, v.y );
		}
	}
	fclose( fp );

	return true;
}

bool ArrayUtils::saveTXT( const Array2D< float4 >& array, const char* filename )
{
	FILE* fp = fopen( filename, "w" );
	if( fp == NULL )
	{
		return false;
	}

	fprintf( fp, "Size: %d x %d\n", array.width(), array.height() );
	fprintf( fp, "Format: float4\n" );

	for( int y = 0; y < array.height(); ++y )
	{
		for( int x = 0; x < array.width(); ++x )
		{
			float4 v = array( x, y );
			fprintf( fp, "[%d %d]: %f, %f, %f, %f\n", x, y, v.x, v.y, v.z, v.w );
		}
	}
	fclose( fp );

	return true;
}
