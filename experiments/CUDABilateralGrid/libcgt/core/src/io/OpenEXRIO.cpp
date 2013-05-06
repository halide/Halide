#include "io/OpenEXRIO.h"

#if 0

#include <OpenEXR/ImfArray.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImathBox.h>
#include <OpenEXR/ImfPixelType.h>

using namespace Imf;
using namespace Imath;

// static
QVector< QPair< QString, OpenEXRIO::ChannelType > >
	OpenEXRIO::getChannelNamesAndTypes( const char* filename )
{
	QVector< QPair< QString, OpenEXRIO::ChannelType > > channelNamesAndTypesOut;

	InputFile inputFile( filename );
	const ChannelList& channels = inputFile.header().channels();

	for( ChannelList::ConstIterator itr = channels.begin(); itr != channels.end(); ++itr )
	{
		const char* channelName = itr.name();
		const Channel& channel = itr.channel();
		
		QPair< QString, OpenEXRIO::ChannelType > nameTypePair;
		nameTypePair.first = channelName;

		switch( channel.type )
		{
		case Imf::UINT:
			nameTypePair.second = OpenEXRIO::UNSIGNED_INT;
			break;
		case Imf::HALF:
			nameTypePair.second = OpenEXRIO::HALF;
			break;
		case Imf::FLOAT:
			nameTypePair.second = OpenEXRIO::FLOAT;
		}
		
		channelNamesAndTypesOut.append( nameTypePair );
	}

	return channelNamesAndTypesOut;
}

// static
FloatMatrix OpenEXRIO::getFloatBufferByName( const char* filename,
											const char* channelName )
{
#if 0
	InputFile inputFile( filename );
	Box2i dataWindow = inputFile.header().dataWindow();
	int width = dataWindow.max.x - dataWindow.min.x + 1;
	int height = dataWindow.max.y - dataWindow.min.y + 1;

	Array2D< float > arr;
	arr.resizeErase( height, width );

	Slice slice
	(
		Imf::FLOAT,																// type
		( char* )( &arr[0][0] - dataWindow.min.x - dataWindow.min.y * width ),	// base
		sizeof( arr[0][0] ) * 1,												// xStride
		sizeof( arr[0][0] ) * width,											// yStride
		1, 1,																	// x/y sampling
		FLT_MAX																	// fillValue
	);

	FrameBuffer frameBuffer;
	frameBuffer.insert( channelName, slice );

	inputFile.setFrameBuffer( frameBuffer );
	inputFile.readPixels( dataWindow.min.y, dataWindow.max.y );

	// copy data into MatrixFloat
	FloatMatrix out( width, height );
	for( int y = 0; y < height; ++y )
	{
		int yy = height - y - 1;
		for( int x = 0; x < width; ++x )
		{
			out( x, y ) = arr[ yy ][ x ];
		}
	}
	
	return out;
#endif
}

#if 0
// static
void OpenEXRIO::readZBuffer( const char* filename,
							Array2D< float >& zPixels,
							int& width, int& height )
{
	// TODO: check if image has z channel

	InputFile inputFile( filename );
	Box2i dataWindow = inputFile.header().dataWindow();
	width = dataWindow.max.x - dataWindow.min.x + 1;
	height = dataWindow.max.y - dataWindow.min.y + 1;

	zPixels.resizeErase( height, width );

	FrameBuffer frameBuffer;
	frameBuffer.insert
	(
		"Z",																			// name
		Slice
		(
			FLOAT,																		// type
			( char* )( &zPixels[0][0] - dataWindow.min.x - dataWindow.min.y * width ),	// base
			sizeof( zPixels[0][0] ) * 1,												// xStride
			sizeof( zPixels[0][0] ) * width,											// yStride
			1, 1,																		// x/y sampling
			FLT_MAX																		// fillValue
		)
	);

	inputFile.setFrameBuffer( frameBuffer );
	inputFile.readPixels( dataWindow.min.y, dataWindow.max.y );
}
#endif

#endif
