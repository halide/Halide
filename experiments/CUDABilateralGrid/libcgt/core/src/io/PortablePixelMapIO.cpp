#include "io/PortablePixelMapIO.h"

#include <cassert>
#include <QFile>
#include <QDataStream>
#include <QTextStream>
#include <color/ColorUtils.h>

// static
bool PortablePixelMapIO::writeRGB( QString filename,								  
								  ubyte* aubRGBArray,
								  int width, int height,								  
								  bool yAxisPointsUp )
{
	assert( aubRGBArray != NULL );

	QFile outputFile( filename );

	// try to open the file in write only mode
	if( !( outputFile.open( QIODevice::WriteOnly ) ) )
	{
		return false;
	}

	QTextStream outputTextStream( &outputFile );
	outputTextStream.setCodec( "ISO-8859-1" );
	outputTextStream << "P6\n";
	outputTextStream << width << " " << height << "\n";
	outputTextStream << "255\n";

	outputTextStream.flush();

	QDataStream outputDataStream( &outputFile );	

	if( yAxisPointsUp )
	{
		for( int y = 0; y < height; ++y )
		{
			for( int x = 0; x < width; ++x )
			{
				int yy = height - y - 1;

				int k = 3 * ( yy * width + x );

				outputDataStream << aubRGBArray[ k ];
				outputDataStream << aubRGBArray[ k + 1 ];
				outputDataStream << aubRGBArray[ k + 2 ];
			}
		}
	}
	else
	{
		for( int y = 0; y < height; ++y )
		{
			for( int x = 0; x < width; ++x )
			{
				int k = 3 * ( y * width + x );

				outputDataStream << aubRGBArray[ k ];
				outputDataStream << aubRGBArray[ k + 1 ];
				outputDataStream << aubRGBArray[ k + 2 ];
			}
		}
	}	

	return true;
}

// static
bool PortablePixelMapIO::writeRGB( QString filename,								  
								  float* afRGBArray,
								  int width, int height,								  
								  bool yAxisPointsUp )
{
	assert( afRGBArray != NULL );

	QFile outputFile( filename );

	// try to open the file in write only mode
	if( !( outputFile.open( QIODevice::WriteOnly ) ) )
	{
		return false;
	}

	QTextStream outputTextStream( &outputFile );
	outputTextStream.setCodec( "ISO-8859-1" );
	outputTextStream << "P6\n";
	outputTextStream << width << " " << height << "\n";
	outputTextStream << "255\n";

	outputTextStream.flush();

	QDataStream outputDataStream( &outputFile );	

	if( yAxisPointsUp )
	{
		for( int y = 0; y < height; ++y )
		{
			for( int x = 0; x < width; ++x )
			{
				int yy = height - y - 1;

				int k = 3 * ( yy * width + x );

				outputDataStream << ColorUtils::floatToUnsignedByte( afRGBArray[ k ] );
				outputDataStream << ColorUtils::floatToUnsignedByte( afRGBArray[ k + 1 ] );
				outputDataStream << ColorUtils::floatToUnsignedByte( afRGBArray[ k + 2 ] );
			}
		}
	}
	else
	{
		for( int y = 0; y < height; ++y )
		{
			for( int x = 0; x < width; ++x )
			{
				int k = 3 * ( y * width + x );

				outputDataStream << ColorUtils::floatToUnsignedByte( afRGBArray[ k ] );
				outputDataStream << ColorUtils::floatToUnsignedByte( afRGBArray[ k + 1 ] );
				outputDataStream << ColorUtils::floatToUnsignedByte( afRGBArray[ k + 2 ] );
			}
		}
	}	

	return true;
}
