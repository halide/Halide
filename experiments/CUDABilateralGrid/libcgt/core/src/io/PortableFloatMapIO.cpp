#include "io/PortableFloatMapIO.h"

#include <cassert>
#include <QFile>
#include <QDataStream>
#include <QTextStream>
#include <vecmath/Vector3f.h>

// static
bool PortableFloatMapIO::read( QString filename,
							  bool yAxisPointsUp,
							  float** pafPixels,
							  int* piWidth, int* piHeight,
							  int* pnComponents,
							  float* pfScale )
{
	assert( pafPixels != NULL );
	assert( piWidth != NULL );
	assert( piHeight != NULL );
	assert( pnComponents != NULL );
	assert( pfScale != NULL );	

	QFile inputFile( filename );

	// try to open the file in read only mode
	if( !( inputFile.open( QIODevice::ReadOnly ) ) )
	{
		return false;
	}

	QTextStream inputTextStream( &inputFile );
	inputTextStream.setCodec( "ISO-8859-1" );

	// read header
	QString qsType;
	QString qsWidth;
	QString qsHeight;
	QString qsScale;	

	int width;
	int height;
	int nComponents;
	float scale;

	inputTextStream >> qsType;
	if( qsType == "Pf" )
	{
		nComponents = 1;
	}
	else if( qsType == "PF" )
	{
		nComponents = 3;
	}
	else
	{
		return false;
	}

	inputTextStream >> qsWidth >> qsHeight >> qsScale;

	// close the text stream
	inputTextStream.setDevice( NULL );
	inputFile.close();

	// now reopen it again in binary mode
	if( !( inputFile.open( QIODevice::ReadOnly ) ) )
	{
		return false;
	}

	int headerLength = qsType.length() + qsWidth.length() + qsHeight.length() + qsScale.length() + 4;

	width = qsWidth.toInt();
	height = qsHeight.toInt();
	scale = qsScale.toFloat();

	QDataStream inputDataStream( &inputFile );
	inputDataStream.skipRawData( headerLength );

	float* pixels = new float[ nComponents * width * height ];
	char* pixelsByteArray = reinterpret_cast< char* >( pixels );	

	if( yAxisPointsUp && height > 1 )
	{
		for( int y = 0; y < height; ++y )
		{
			for( int x = 0; x < width; ++x )
			{
				int yy = height - y - 1;
				int index = yy * width + x;				

				inputDataStream.readRawData
					(
					reinterpret_cast< char* >( &( pixels[ index ] ) ),
					4 * sizeof( float )
					);
			}
		}
	}
	else
	{
		inputDataStream.readRawData( pixelsByteArray, nComponents * width * height * sizeof( float ) );
	}

	*pafPixels = pixels;
	*piWidth = width;
	*piHeight = height;
	*pnComponents = nComponents;
	*pfScale = scale;

	return true;
}

// static
bool PortableFloatMapIO::writeGreyscale( QString filename,
										float* afLuminance,
										int width, int height,
										bool yAxisPointsUp,
										float scale,
										bool littleEndian )
{
	assert( afLuminance != NULL );
	assert( scale > 0 );

	QFile outputFile( filename );

	// try to open the file in write only mode
	if( !( outputFile.open( QIODevice::WriteOnly ) ) )
	{
		return false;
	}

	if( littleEndian )
	{
		scale *= -1;
	}

	QTextStream outputTextStream( &outputFile );
	outputTextStream.setCodec( "ISO-8859-1" );
	outputTextStream << "Pf\n";
	outputTextStream << width << " " << height << "\n";
	outputTextStream << scale << "\n";

	outputTextStream.flush();

	QDataStream outputDataStream( &outputFile );
	if( littleEndian )
	{
		outputDataStream.setByteOrder( QDataStream::LittleEndian );
	}
	else
	{
		outputDataStream.setByteOrder( QDataStream::BigEndian );
	}

	if( yAxisPointsUp )
	{
		for( int y = 0; y < height; ++y )
		{
			for( int x = 0; x < width; ++x )
			{
				int yy = height - y - 1;
				int index = yy * width + x;
				float luminance = afLuminance[ index ];

				outputDataStream << luminance;
			}
		}
	}
	else
	{
		for( int y = 0; y < height; ++y )
		{
			for( int x = 0; x < width; ++x )
			{
				int index = y * width + x;
				float luminance = afLuminance[ index ];

				outputDataStream << luminance;
			}
		}
	}

	return true;
}

// static
bool PortableFloatMapIO::writeRGB( QString filename,
								  Vector3f* avRGB,
								  int width, int height,
								  float scale )
{
	assert( avRGB != NULL );

	QFile outputFile( filename );
	
	// try to open the file in write only mode
	if( !( outputFile.open( QIODevice::WriteOnly ) ) )
	{
		return false;
	}

	QTextStream outputTextStream( &outputFile );
	outputTextStream.setCodec( "ISO-8859-1" );
	outputTextStream << "PF\n";
	outputTextStream << width << " " << height << "\n";
	outputTextStream << scale << "\n"; // TODO: fix this
	
	outputTextStream.flush();

	QDataStream outputDataStream( &outputFile );
	if( scale < 0 )
	{
		outputDataStream.setByteOrder( QDataStream::LittleEndian );
	}
	else
	{
		outputDataStream.setByteOrder( QDataStream::BigEndian );
	}

	for( int y = 0; y < height; ++y )
	{
		for( int x = 0; x < width; ++x )
		{
			int k = y * width + x;
			Vector3f color = avRGB[ k ];

			outputDataStream << color[0];
			outputDataStream << color[1];
			outputDataStream << color[2];
		}
	}

	return true;
}

// static
bool PortableFloatMapIO::writeRGB( QString filename,								  
								  float* afRGBArray,
								  int width, int height,
								  float scale,
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
	outputTextStream << "PF\n";
	outputTextStream << width << " " << height << "\n";
	outputTextStream << scale << "\n"; // TODO: fix this

	outputTextStream.flush();

	QDataStream outputDataStream( &outputFile );
	if( scale < 0 )
	{
		outputDataStream.setByteOrder( QDataStream::LittleEndian );
	}
	else
	{
		outputDataStream.setByteOrder( QDataStream::BigEndian );
	}

	if( yAxisPointsUp )
	{
		for( int y = 0; y < height; ++y )
		{
			for( int x = 0; x < width; ++x )
			{
				int yy = height - y - 1;

				int k = 3 * ( yy * width + x );

				outputDataStream << afRGBArray[ k ];
				outputDataStream << afRGBArray[ k + 1 ];
				outputDataStream << afRGBArray[ k + 2 ];
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

				outputDataStream << afRGBArray[ k ];
				outputDataStream << afRGBArray[ k + 1 ];
				outputDataStream << afRGBArray[ k + 2 ];
			}
		}
	}	

	return true;
}
