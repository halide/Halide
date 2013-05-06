#ifndef OPEN_EXR_IO_H
#define OPEN_EXR_IO_H

#if 0

#include <QPair>
#include <QVector>
#include <vecmath/MatrixT.h>

class OpenEXRIO
{
public:

	enum ChannelType
	{
		UNSIGNED_INT = 0,	// 32-bit unsigned int
		HALF = 1,			// 16-bit float
		FLOAT = 2			// 32-bit float
	};

	// returns a vector of pairs
	// each pair is a channel name and a channel type	
	static QVector< QPair< QString, ChannelType > >
		getChannelNamesAndTypes( const char* filename );

	static FloatMatrix getFloatBufferByName( const char* filename,
		const char* channelName );

#if 0
	static void readZBuffer( const char* filename,
		Imf::Array2D< float >& zPixels,
		int& width, int& height );
#endif
};

#endif

#endif
