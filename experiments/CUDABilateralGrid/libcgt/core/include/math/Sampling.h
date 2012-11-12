#ifndef SAMPLING_H
#define SAMPLING_H

class Random;
class SamplingPatternND;

#include "vecmath/Vector3f.h"

class Sampling
{
public:

	// TODO: fix this, and sampling pattern
	// the interface sucks!

	// populates pPattern with a latin hypercube sampling pattern
	static void latinHypercubeSampling( Random& random, SamplingPatternND* pPattern );

	// TODO: rename to u0, u1, return a Vector2f
	static void uniformSampleDisc( float u1, float u2,
		float* px, float* py );

	// TODO: rename to u0, u1, return a Vector2f
	static void concentricSampleDisc( float u1, float u2,
		float* px, float* py );	

	// given uniform random numbers u0, u1 in [0,1]
	// returns the barycentric coordinates of a random point
	// in the triangle
	static Vector3f areaSampleTriangle( float u0, float u1 );

};

#endif
