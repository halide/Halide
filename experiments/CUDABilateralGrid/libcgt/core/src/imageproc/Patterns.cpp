#include "imageproc/Patterns.h"

// static
Reference< Image4f > Patterns::createCheckerboard( int width, int height, int checkerSize,
												  const Vector4f& whiteColor, const Vector4f& blackColor )
{
	Reference< Image4f > checkerboard = new Image4f( width, height );

	int nBlocksX = 1 + width / checkerSize;
	int nBlocksY = 1 + height / checkerSize;

	bool rowIsWhite = true;
	bool isWhite;

	for( int by = 0; by < nBlocksY; ++by )
	{
		isWhite = rowIsWhite;
		for( int bx = 0; bx < nBlocksX; ++bx )
		{
			for( int y = by * checkerSize; ( y < ( by + 1 ) * checkerSize ) && ( y < height ); ++y )
			{
				for( int x = bx * checkerSize; ( x < ( bx + 1 ) * checkerSize ) && ( x < width ); ++x )
				{
					if( isWhite )
					{
						checkerboard->setPixel( x, y, whiteColor );
					}
					else
					{
						checkerboard->setPixel( x, y, blackColor );
					}
				}
			}

			isWhite = !isWhite;
		}
		rowIsWhite = !rowIsWhite;
	}

	return checkerboard;
}

// static
Reference< Image1f > Patterns::createRandom( int width, int height, Random& random )
{
	Reference< Image1f > im = new Image1f( width, height );
	float* pixels = im->pixels();

	for( int i = 0; i < width * height; ++i )
	{
		pixels[ i ] = random.nextFloat();
	}

	return im;
}

// static
Reference< Image4f > Patterns::createRandomFloat4( int width, int height, Random& random )
{
	Reference< Image4f > im = new Image4f( width, height );
	float* pixels = im->pixels();

	for( int i = 0; i < 4 * width * height; ++i )
	{
		pixels[ i ] = random.nextFloat();
	}

	return im;
}
