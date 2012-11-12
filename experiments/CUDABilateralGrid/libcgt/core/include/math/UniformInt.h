#ifndef UNIFORM_INT_H
#define UNIFORM_INT_H

#include <common/BasicTypes.h>
#include <random>

// simple wrapper around the STL Mersenne Twister
// hides a bunch of options and renames functions to be nicer

class UniformInt
{
public:

	// initialize a pseudo-random number generator with an optional seed
	// that returns values between in [0,std::numeric_limits< int >::max())
	UniformInt( uint seed = std::mt19937::default_seed );

	// initialize a pseudo-random number generator with an optional seed
	// that returns values in [0,hi)
	UniformInt( int hi, uint seed = std::mt19937::default_seed );

	// initialize a pseudo-random number generator with an optional seed
	// that returns values in [lo,hi)
	UniformInt( int lo, int hi, uint seed = std::mt19937::default_seed );
	
	int nextInt();	

private:

	std::mt19937 m_engine;
	std::uniform_int_distribution< int > m_distribution;
};

#endif // UNIFORM_INT_H
