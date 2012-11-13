#ifndef PATTERNS_H
#define PATTERNS_H

#include "common/Reference.h"
#include "math/Random.h"
#include "vecmath/Vector4f.h"

#include "Image1f.h"
#include "Image4f.h"

class Patterns
{
public:

	static Reference< Image4f > createCheckerboard( int width, int height, int checkerSize,
		const Vector4f& whiteColor = Vector4f( 1.f, 1.f, 1.f, 1.f ),
		const Vector4f& blackColor = Vector4f( 0.8f, 0.8f, 0.8f, 1.f ) );

	static Reference< Image1f > Patterns::createRandom( int width, int height, Random& random );
	static Reference< Image4f > Patterns::createRandomFloat4( int width, int height, Random& random );

};

#endif // PATTERNS_H
