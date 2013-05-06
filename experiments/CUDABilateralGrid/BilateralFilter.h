#ifndef BILATERAL_FILTER_H
#define BILATERAL_FILTER_H

#include <CUDAVector2D.h>
#include <CUDAVector3D.h>

class BilateralFilter
{
public:

	BilateralFilter( int imageWidth, int imageHeight,
		float sigmaSpatial = 16.f, float sigmaRange = 0.1f,
		float edgeMin = 0.f, float edgeMax = 1.f,
		bool cross = false );

	void setup( int imageWidth, int imageHeight,
		float sigmaSpatial, float sigmaRange,
		float edgeMin, float edgeMax,
		bool cross );

    void setInput(const Array2D< float >& input);
    void getOutput(Array2D< float >& output);
	// given input in [0,1],
	// output <-- bilateral_filter( input )
	void apply();

	// filter "data" using edges from "edge"
	// output <-- cross_bilateral_filter( data, edge )
	void applyCross( const Array2D< float >& data, const Array2D< float >& edge,
		Array2D< float >& output );

private:

	void createGrid();
	void createCrossGrid();

	void blurGrid();

	void sliceGrid();
	void sliceCrossGrid();

	float m_samplingSpatial;
	float m_samplingRange;

	float m_edgeMin;
	float m_edgeRange;

	int m_paddingXY;
	int m_paddingZ;

	CUDAVector2D< float > md_dataImage;
	CUDAVector2D< float > md_edgeImage;
	CUDAVector3D< float2 > md_grids[2];
	CUDAVector2D< float > md_outputImage;

	// TODO: test cudaArray / tex3D() combination
};

#endif // BILATERAL_FILTER_H
