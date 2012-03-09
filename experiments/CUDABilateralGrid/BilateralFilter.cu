#include "BilateralFilter.h"
#include <CUDAVector3D.h>
#include <MathUtils.h>

#include <cutil_math.h>

// TODO: be very careful with this variable
texture< float2, cudaTextureType2D, cudaReadModeElementType > g_gridTexture( 0, cudaFilterModeLinear );

// TODO: pass in reciprocal samplingSpatial, samplingRange, inputRange, etc
// TODO: padding is a constant 3

// TODO: refactor kernels into another file to decouple from C++ interface
__global__
void createGridKernel( DeviceVector2D< float > inputImage,
					  float inputMin, float inputRange,
					  float samplingSpatial, float samplingRange,
					  int paddingXY, int paddingZ,
					  DeviceArray3D< float2 > outputGrid )
{
	int x = blockIdx.x * blockDim.x + threadIdx.x;
	int y = blockIdx.y * blockDim.y + threadIdx.y;

	if( x >= inputImage.width ||
		y >= inputImage.height )
	{
		return;
	}
	
	// load data
	// and normalize to [0,1]
	float lum = inputImage( x, y );
	lum = ( lum - inputMin ) / inputRange;

	int gx = roundToInt( x / samplingSpatial ) + paddingXY;
	int gy = roundToInt( y / samplingSpatial ) + paddingXY;
	int gz = roundToInt( lum / samplingRange ) + paddingZ;

	float2* pOutputPixel = &( outputGrid( gx, gy, gz ) );
	atomicAdd( &( pOutputPixel->x ), lum );
	atomicAdd( &( pOutputPixel->y ), 1 );
}

__global__
void createCrossGridKernel( DeviceVector2D< float > dataImage,
						   DeviceVector2D< float > edgeImage,
						   float edgeMin, float edgeRange,
						   float samplingSpatial, float samplingRange,
						   int paddingXY, int paddingZ,
						   DeviceArray3D< float2 > outputGrid )
{
	int x = blockIdx.x * blockDim.x + threadIdx.x;
	int y = blockIdx.y * blockDim.y + threadIdx.y;

	if( x >= dataImage.width ||
		y >= dataImage.height )
	{
		return;
	}
	
	// load edge term
	// and normalize to [0,1]
	float e = edgeImage( x, y );	
	e = ( e - edgeMin ) / edgeRange;

	int gx = roundToInt( x / samplingSpatial ) + paddingXY;
	int gy = roundToInt( y / samplingSpatial ) + paddingXY;
	int gz = roundToInt( e / samplingRange ) + paddingZ;

	// put pixel from "data" into grid
	// at location dictated by "edge"
	float d = dataImage( x, y );

	float2* pOutputPixel = &( outputGrid( gx, gy, gz ) );
	atomicAdd( &( pOutputPixel->x ), d );
	atomicAdd( &( pOutputPixel->y ), 1 );
}

__global__
void blurGridKernel( DeviceArray3D< float2 > inputGrid,
					int3 delta, // blur direction
					DeviceArray3D< float2 > outputGrid )
{
	int x = blockIdx.x * blockDim.x + threadIdx.x;
	int y = blockIdx.y * blockDim.y + threadIdx.y;
	int z = blockIdx.z * blockDim.z + threadIdx.z;

	if( x >= inputGrid.width ||
		y >= inputGrid.height ||
		z >= inputGrid.depth )
	{
		return;
	}

	// compute delta
	int3 xyz = make_int3( x, y, z );

	int3 lo = make_int3( 0 );
	int3 hi = make_int3( inputGrid.width - 1, inputGrid.height - 1, inputGrid.depth - 1 );
	
	float2 v0 = inputGrid( clamp( xyz - 2 * delta, lo, hi ) );
	float2 v1 = inputGrid( clamp( xyz - delta, lo, hi ) );
	float2 v2 = inputGrid( clamp( xyz, lo, hi ) );
	float2 v3 = inputGrid( clamp( xyz + delta, lo, hi ) );
	float2 v4 = inputGrid( clamp( xyz + 2 * delta, lo, hi ) );

	float2 sum =
	(
		0.135335283236613f * v0 +
		0.606530659712633f * v1 +
		v2 + 
		0.606530659712633f * v3 +
		0.135335283236613f * v4
	);

	// early division on the last pass
	// TODO: use a separate pass? 
	if( delta.z == 1 )
	{
		if( sum.y == 0 )
		{
			outputGrid( x, y, z ) = make_float2( 0, 0 );
		}
		else
		{
			outputGrid( x, y, z ) = make_float2( sum.x / sum.y, 0 );
		}
	}
	else
	{
		outputGrid( x, y, z ) = sum;
	}
}

__global__
void sliceGridKernel( DeviceVector2D< float > inputImage,
					 int gridWidth, int gridHeight,
					 float inputMin, float inputRange,
					 float samplingSpatial, float samplingRange,
					 int paddingXY, int paddingZ,
					 DeviceVector2D< float > outputImage )
{
	int x = blockIdx.x * blockDim.x + threadIdx.x;
	int y = blockIdx.y * blockDim.y + threadIdx.y;

	if( x >= inputImage.width ||
		y >= inputImage.height )
	{
		return;
	}
	
	// load data
	// and normalize to [0,1]
	float lum = inputImage( x, y );
	lum = ( lum - inputMin ) / inputRange;

	float gx = x / samplingSpatial + paddingXY;
	float gy = y / samplingSpatial + paddingXY;
	float gz = lum / samplingRange + paddingZ;

	int gz0 = static_cast< int >( gz );
	int gz1 = gz0 + 1;
	float gzFrac = gz - gz0;	

	float v0 = tex2D( g_gridTexture, gx, gz0 * gridHeight + gy ).x;
	float v1 = tex2D( g_gridTexture, gx, gz1 * gridHeight + gy ).x;
	
	float v = lerp( v0, v1, gzFrac );
	outputImage( x, y ) = v;
}

BilateralFilter::BilateralFilter( int imageWidth, int imageHeight,
								 float sigmaSpatial, float sigmaRange,
								 float edgeMin, float edgeMax,
								 bool cross )
{
	setup( imageWidth, imageHeight,
		sigmaSpatial, sigmaRange,
		edgeMin, edgeMax,
		cross );
}

void BilateralFilter::setup( int imageWidth, int imageHeight,
							float sigmaSpatial, float sigmaRange,
							float edgeMin, float edgeMax,
							bool cross )
{
	md_dataImage.resize( imageWidth, imageHeight );
	if( cross )
	{
		md_edgeImage.resize( imageWidth, imageHeight );
	}

	md_outputImage.resize( imageWidth, imageHeight );	

	/*
	// If we allowed variable sampling rates,
	// we'd need to compute derived sigma parameters
	// (gaussian sigmas in the downsampled space)
	float derivedSigmaSpatial = sigmaSpatial / samplingSpatial;
	float derivedSigmaRange = sigmaRange / samplingRange;

	m_paddingXY = static_cast< int >( 2 * derivedSigmaSpatial ) + 1;
	m_paddingZ = static_cast< int >( 2 * derivedSigmaRange ) + 1;
	*/

	m_samplingSpatial = sigmaSpatial;
	m_samplingRange = sigmaRange;

	m_edgeMin = edgeMin;
	m_edgeRange = edgeMax - edgeMin;

	m_paddingXY = 3;
	m_paddingZ = 3;

	// allocate 3D grid	
	int gridWidth = static_cast< int >( ( imageWidth - 1 ) / m_samplingSpatial ) + 1 + 2 * m_paddingXY;
	int gridHeight = static_cast< int >( ( imageHeight - 1 ) / m_samplingSpatial ) + 1 + 2 * m_paddingXY;
	int gridDepth = static_cast< int >( m_edgeRange / m_samplingRange ) + 1 + 2 * m_paddingZ;

	md_grids[0].resize( gridWidth, gridHeight, gridDepth );
	md_grids[1].resize( gridWidth, gridHeight, gridDepth );
}

void BilateralFilter::setInput(const Array2D< float >& input)
{
	md_dataImage.copyFromHost( input );
}

void BilateralFilter::getOutput(Array2D< float >& output)
{
	md_outputImage.copyToHost( output );
}

void BilateralFilter::apply(  )
{
	// input --> md_dataImage
	//md_dataImage.copyFromHost( input );

	createGrid();

	blurGrid();
	
	sliceGrid();

	// md_outputImage --> output
	//md_outputImage.copyToHost( output );
}

void BilateralFilter::applyCross( const Array2D< float >& data, const Array2D< float >& edge,
	Array2D< float >& output )
{
	md_dataImage.copyFromHost( data );
	md_edgeImage.copyFromHost( edge );

	createCrossGrid();

	blurGrid();
	
	sliceCrossGrid();

	// md_outputImage --> output
	md_outputImage.copyToHost( output );
}

void BilateralFilter::createGrid()
{
	// TODO: launching a kernel to do a clear
	// might actually be faster than a cudaMemset( 0 )
	md_grids[0].clear();

	dim3 blockDim( 16, 16, 1 );

	int gx = numBins( md_dataImage.width(), blockDim.x );
	int gy = numBins( md_dataImage.height(), blockDim.y );
	dim3 gridDim( gx, gy, 1 );

	createGridKernel<<< gridDim, blockDim >>>
	(
		md_dataImage.deviceVector(),
		m_edgeMin, m_edgeRange,
		m_samplingSpatial, m_samplingRange,
		m_paddingXY, m_paddingZ,
		md_grids[0].deviceArray()
	);
}

void BilateralFilter::createCrossGrid()
{
	// TODO: launching a kernel to do a clear
	// might actually be faster than a cudaMemset( 0 )
	md_grids[0].clear();

	dim3 blockDim( 16, 16, 1 );

	int gx = numBins( md_dataImage.width(), blockDim.x );
	int gy = numBins( md_dataImage.height(), blockDim.y );
	dim3 gridDim( gx, gy, 1 );

	createCrossGridKernel<<< gridDim, blockDim >>>
	(
		md_dataImage.deviceVector(),
		md_edgeImage.deviceVector(),
		m_edgeMin, m_edgeRange,
		m_samplingSpatial, m_samplingRange,
		m_paddingXY, m_paddingZ,
		md_grids[0].deviceArray()
	);
}

void BilateralFilter::blurGrid()
{
	Array3D< float2 > tmp( md_grids[0].width(), md_grids[0].height(), md_grids[0].depth() );

	dim3 blockDim( 8, 8, 8 );
    //dim3 blockDim( 8, 8, 1 ); // jrk

	int gx = numBins( md_grids[0].width(), blockDim.x );
	int gy = numBins( md_grids[0].height(), blockDim.y );
	int gz = numBins( md_grids[0].depth(), blockDim.z );
	dim3 gridDim( gx, gy, gz );

	// blur x
	blurGridKernel<<< gridDim, blockDim >>>
	(
		md_grids[0].deviceArray(),
		make_int3( 1, 0, 0 ),
		md_grids[1].deviceArray()
	);

	// blur y
	blurGridKernel<<< gridDim, blockDim >>>
	(
		md_grids[1].deviceArray(),
		make_int3( 0, 1, 0 ),
		md_grids[0].deviceArray()
	);

	// blur z
	blurGridKernel<<< gridDim, blockDim >>>
	(
		md_grids[0].deviceArray(),
		make_int3( 0, 0, 1 ),
		md_grids[1].deviceArray()
	);
}

void BilateralFilter::sliceGrid()
{
	size_t offset;
	cudaChannelFormatDesc cd = cudaCreateChannelDesc< float2 >();
	void* ptr = md_grids[1].pitchedPointer().ptr;

	cudaBindTexture2D
	(
		&offset,
		&g_gridTexture,
		ptr,
		&cd,
		md_grids[1].width(),
		md_grids[1].height() * md_grids[1].depth(),
		md_grids[1].rowPitch()
	);

	dim3 blockDim( 16, 16, 1 );
    //dim3 blockDim( m_samplingSpatial, m_samplingSpatial, 1 ); //jrk
    //dim3 blockDim( 8, 8, 1 ); //jrk
	int gx = numBins( md_dataImage.width(), blockDim.x );
	int gy = numBins( md_dataImage.height(), blockDim.y );
	dim3 gridDim( gx, gy, 1 );

	sliceGridKernel<<< gridDim, blockDim >>>
	(
		md_dataImage.deviceVector(),
		md_grids[1].width(), md_grids[1].height(),
		m_edgeMin, m_edgeRange,
		m_samplingSpatial, m_samplingRange,
		m_paddingXY, m_paddingZ,
		md_outputImage.deviceVector()
	);

	cudaUnbindTexture( &g_gridTexture );
}

void BilateralFilter::sliceCrossGrid()
{
	size_t offset;
	cudaChannelFormatDesc cd = cudaCreateChannelDesc< float2 >();
	void* ptr = md_grids[1].pitchedPointer().ptr;

	cudaBindTexture2D
	(
		&offset,
		&g_gridTexture,
		ptr,
		&cd,
		md_grids[1].width(),
		md_grids[1].height() * md_grids[1].depth(),
		md_grids[1].rowPitch()
	);

	dim3 blockDim( 16, 16, 1 );
	int gx = numBins( md_dataImage.width(), blockDim.x );
	int gy = numBins( md_dataImage.height(), blockDim.y );
	dim3 gridDim( gx, gy, 1 );

	sliceGridKernel<<< gridDim, blockDim >>>
	(
		md_edgeImage.deviceVector(),
		md_grids[1].width(), md_grids[1].height(),
		m_edgeMin, m_edgeRange,
		m_samplingSpatial, m_samplingRange,
		m_paddingXY, m_paddingZ,
		md_outputImage.deviceVector()
	);

	cudaUnbindTexture( &g_gridTexture );
}
