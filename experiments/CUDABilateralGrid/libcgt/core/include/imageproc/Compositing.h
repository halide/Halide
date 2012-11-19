#ifndef COMPOSITING_H
#define COMPOSITING_H

#include "common/Reference.h"
#include "Image3ub.h"
#include "Image4f.h"
#include "Image4ub.h"

class Compositing
{
public:

	// classic compositing operation:
	// C_o = a_f * C_f + ( 1 - a_f ) * C_b
	// a_o = a_f + a_b * ( 1 - a_f )
	// composites foreground over background into the buffer "composite" and returns it
	// if composite is NULL, then a new buffer will be created
	static Reference< Image4f > compositeOver( Reference< Image4f > foreground,
		Reference< Image4f > background,
		Reference< Image4f > composite = NULL );

	// given the composite image "compositeRGBA"
	// and the foreground image "foregroundRGBA" (probably from matting)
	// divides out the alpha to extract the background color in "backgroundRGBA" and returns it
	// if backgroundRGBA is NULL, then a new buffer will be created
	// TODO: get rid of repeated code
	static Reference< Image4f > extractBackgroundColor( Reference< Image4f > composite,
		Reference< Image4f > foreground,
		Reference< Image4f > background = NULL );
	static Reference< Image4f > extractBackgroundColor( Reference< Image3ub > composite,
		Reference< Image4f > foreground,
		Reference< Image4f > background );
	static Reference< Image4ub > extractBackgroundColor( Reference< Image4ub > composite,
		Reference< Image4ub > foreground,
		Reference< Image4ub > background );
	static Reference< Image4ub > extractBackgroundColor( Reference< Image3ub > composite,
		Reference< Image4ub > foreground,
		Reference< Image4ub > background );

private:

	static Vector4f extractBackgroundColor( const Vector4f& composite, const Vector4f& foreground );
};

#endif // COMPOSITING_H
