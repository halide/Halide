#ifndef RANDOM_H
#define RANDOM_H

#include <common/BasicTypes.h>

#include "MersenneTwister.h"

// simple wrapper around the Mersenne Twister
// hides a bunch of options and renames functions to be nicer
class Random
{
public:

	Random(); // from /dev/random or from clock()
	Random( int seed ); // seed from integer

	// [0,1]
	double nextDouble();

	// [0,1]
	float nextFloat();

	// [0, 2^32 - 1]
	uint nextInt();

	double nextDoubleRange( double lo, double hi );

	float nextFloatRange( float lo, float hi );

	// [0,n] for n < 2^32
	int nextIntInclusive( int n );

	// [0,n) for n < 2^32
	int nextIntExclusive( int n );

private:

	MTRand m_mtRand;

};

#endif // RANDOM_H
