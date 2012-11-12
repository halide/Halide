#ifndef SAMPLING_PATTERN_ND_H
#define SAMPLING_PATTERN_ND_H

class SamplingPatternND
{
public:

	SamplingPatternND( int nSamples, int nDimensions );
	virtual ~SamplingPatternND();

	// get the jth sample
	// sample[] should have length of at least nDimensions
	void getSample( int j, float sample[] );

	int getNumSamples();
	int getNumDimensions();

	// returns the raw data
	// to be populated by a sampling algorithm
	// afSamples is stored as:
	// if nDimensions = 5
	// mem: 01234 56780 ABCDE
	//      00000 11111 22222
	//      s0    s1    s2
	float* getSamples();

private:

	int m_nSamples;
	int m_nDimensions;
	float* m_afSamples;
};

#endif
