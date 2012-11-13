#include "math/Sampling.h"

#include <algorithm>
#include <cmath>

#include "math/MathUtils.h"
#include "math/Random.h"
#include "math/SamplingPatternND.h"

using namespace std;

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
void Sampling::latinHypercubeSampling( Random& random, SamplingPatternND* pPattern )
{
	int nDimensions = pPattern->getNumDimensions();
	int nSamples = pPattern->getNumSamples();
	float* samples = pPattern->getSamples();

	// generate samples along diagonal boxes
	float delta = 1.0f / nSamples;
	for( int i = 0; i < nSamples; ++i )
	{
		for( int j = 0; j < nDimensions; ++j )
		{
			samples[ i * nDimensions + j ] = ( i + static_cast< float >( random.nextDouble() ) ) * delta;
		}
	}

	// permute samples in each dimension
	for( int i = 0; i < nDimensions; ++i )
	{
		for( int j = 0; j < nSamples; ++j )
		{
			int otherSample = random.nextIntInclusive( nSamples - 1 );
			swap( samples[ j * nDimensions + i ], samples[ otherSample * nDimensions + i ] );
		}
	}
}

// static
void Sampling::uniformSampleDisc( float u1, float u2,
								 float* px, float* py )
{
	float r = sqrt( u1 );
	float theta = static_cast< float >( 2.0f * MathUtils::PI * u2 );
	*px = r * cos( theta );
	*py = r * sin( theta );
}

// static
void Sampling::concentricSampleDisc( float u1, float u2,
								 float* px, float* py )
{
	float r, theta;

	// Map uniform random numbers to [-1, 1]^2
	float sx = 2 * u1 - 1;
	float sy = 2 * u2 - 1;

	// Map square to (r, theta)
	// Handle degeneracy at the origin
	if( sx == 0.0 && sy == 0.0 )
	{
		*px = 0.0;
		*py = 0.0;
		return;
	}
	
	if( sx >= -sy )
	{
		if( sx > sy )
		{
			// Handle first region of disk
			r = sx;
			if( sy > 0.0 )
			{
				theta = sy / r;
			}
			else
			{
				theta = 8.0f + sy / r;
			}
		}
		else
		{
			// Handle second region of disk
			r = sy;
			theta = 2.0f - sx / r;
		}
	}
	else
	{
		if( sx <= sy )
		{
			// Handle third region of disk
			r = -sx;
			theta = 4.0f - sy / r;
		}
		else
		{
			// Handle fourth region of disk
			r = -sy;
			theta = 6.0f + sx / r;
		}
	}
	
	theta *= static_cast< float >( MathUtils::PI / 4.f );
	*px = r * cos( theta );
	*py = r * sin( theta );
}

// static
Vector3f Sampling::areaSampleTriangle( float u0, float u1 )
{
	if( u0 + u1 > 1 )
	{
		u0 = 1 - u0;
		u1 = 1 - u1;
	}

	return Vector3f( 1 - u0 - u1, u0, u1 );
}