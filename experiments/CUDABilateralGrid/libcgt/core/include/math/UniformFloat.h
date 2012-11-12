#ifndef UNIFORM_FLOAT_H
#define UNIFORM_FLOAT_H

#include <common/BasicTypes.h>
#include <random>

// simple wrapper around the STL Mersenne Twister
// hides a bunch of options and renames functions to be nicer

class UniformFloat
{
public:

	UniformFloat( uint seed = std::mt19937::default_seed );

	float nextFloat();
	float nextRange( float lo, float hi );

private:

	std::mt19937 m_engine;
	std::uniform_real_distribution< float > m_distribution;
};

#endif // UNIFORM_FLOAT_H
