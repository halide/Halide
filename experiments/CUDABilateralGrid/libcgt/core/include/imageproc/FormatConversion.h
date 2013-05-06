#ifndef FORMAT_CONVERSION_H
#define FORMAT_CONVERSION_H

#include "imageproc/Image3ub.h"
#include "imageproc/Image4f.h"

class FormatConversion
{
public:

	static void image3ubToImage4f( const Image3ub& source, Image4f& destination, bool flipUpDown, float fillAlpha = 1.f );
	static void image4fToImage3ub( const Image4f& source, Image3ub& destination, bool flipUpDown );

};

#endif // FORMAT_CONVERSION_H
