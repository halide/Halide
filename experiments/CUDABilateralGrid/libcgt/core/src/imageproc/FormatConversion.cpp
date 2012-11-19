#include "imageproc/FormatConversion.h"

#include "color/ColorUtils.h"

// static
void FormatConversion::image3ubToImage4f( const Image3ub& source, Image4f& destination, bool flipUpDown, float fillAlpha )
{
	int width = source.width();
	int height = source.height();

	for( int y = 0; y < height; ++y )
	{
		int yy = y;
		if( flipUpDown )
		{
			yy = height - y - 1;
		}

		for( int x = 0; x < width; ++x )
		{
			Vector3i input = source.pixel( x, y );
			Vector3f output = ColorUtils::intToFloat( input );			
			destination.setPixel( x, yy, Vector4f( output, fillAlpha ) );
		}
	}
}

// static
void FormatConversion::image4fToImage3ub( const Image4f& source, Image3ub& destination, bool flipUpDown )
{
	int width = source.width();
	int height = source.height();

	for( int y = 0; y < height; ++y )
	{
		int yy = y;
		if( flipUpDown )
		{
			yy = height - y - 1;
		}

		for( int x = 0; x < width; ++x )
		{
			Vector4f input = source.pixel( x, y );
			Vector3i output = ColorUtils::floatToInt( input ).xyz();
			destination.setPixel( x, yy, output );
		}
	}
}
