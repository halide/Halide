#include "math/SamplingPatternND.h"

SamplingPatternND::SamplingPatternND( int nSamples, int nDimensions ) :

	m_nSamples( nSamples ),
	m_nDimensions( nDimensions )

{
	m_afSamples = new float[ nSamples * nDimensions ];
}

// virtual
SamplingPatternND::~SamplingPatternND()
{
	delete[] m_afSamples;
}

void SamplingPatternND::getSample( int j, float sample[] )
{
	int sampleStartIndex = j * m_nDimensions;
	for( int i = 0; i < m_nDimensions; ++i )
	{
		sample[i] = m_afSamples[ sampleStartIndex + i ];
	}
}

int SamplingPatternND::getNumSamples()
{
	return m_nSamples;
}

int SamplingPatternND::getNumDimensions()
{
	return m_nDimensions;
}

float* SamplingPatternND::getSamples()
{
	return m_afSamples;
}
