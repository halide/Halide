#include <cuda_runtime.h>
#include <cutil_inline.h>

#include "BilateralFilter.h"

#include <color/ColorUtils.h>
#include <imageproc/Image4ub.h>
#include <imageproc/Image4f.h>
#include <time/StopWatch.h>
#include <vecmath/Vector3f.h>

#include <QString>

void testBilateralFilter( const Array2D< float >& input,
	float ss, float sr,
	Array2D< float >& output )
{
	BilateralFilter bf( input.width(), input.height(), ss, sr );
	
	int nIterations = 100;

    
	bf.setInput( input );
	StopWatch sw;

	for( int i = 0; i < nIterations; ++i )
	{
		bf.apply( );
		cudaDeviceSynchronize();
	}

	float ms = sw.millisecondsElapsed();
	bf.getOutput( output );
	printf( "image size: %d x %d\n", input.width(), input.height() );
	printf( "ss = %f, sr = %f\n", ss, sr );
	printf( "Total time = %f ms, ms on average: %f\n",
		ms, ms / nIterations );
}

void testCrossBilateralFilter( const Array2D< float >& data, const Array2D< float >& edge,
	float ss, float sr,
	Array2D< float >& output )
{
	BilateralFilter cbf( data.width(), data.height(), ss, sr, 0.f, 1.f, true );
	
	int nIterations = 100;

	StopWatch sw;

	for( int i = 0; i < nIterations; ++i )
	{
		cbf.applyCross( data, edge, output );
		cudaDeviceSynchronize();
	}

	float ms = sw.millisecondsElapsed();
	printf( "image size: %d x %d\n", data.width(), data.height() );
	printf( "ss = %f, sr = %f\n", ss, sr );
	printf( "Total time = %f ms, ms on average: %f\n",
		ms, ms / nIterations );
}

void saveArrayAsImage( const Array2D< float >& array, QString prefix, float ss, float sr )
{
	Image4f im( array.width(), array.height() );

	for( int y = 0; y < im.height(); ++y )
	{
		for( int x = 0; x < im.width(); ++x )
		{
			float v = array( x, y );
			im.setPixel( x, y, Vector4f( v, v, v, 1 ) );
		}
	}

	QString filename = QString( "%1_%2_%3.png" ).arg( prefix ).arg( ss ).arg( sr );
	printf( "saving output: %s...", qPrintable( filename ) );
	im.flipUD().save( filename );
	printf( "done.\n\n" );
}

#include <CUDAVector2D.h>
#include <common/Reference.h>

int main( int argc, char* argv[] )
{
	int argc2 = 2;
	char* argv2[2] = { "", "-device=1" };

	cudaDeviceProp deviceProp;
    int devID = cutilChooseCudaDevice( argc2, argv2 );
    if( devID < 0 )
	{
       printf( "exiting...\n" );
       cutilExit( argc, argv );
       exit( 0 );
    }
    cutilSafeCall( cudaGetDeviceProperties( &deviceProp, devID ) );

	//Image4f im( "c:/tmp/tulip.png" );
	//Image4f im( "c:/tmp/tulip_1080.png" ); // Jiawen version
	Image4f im( "../../apps/bilateral_grid/input.png" );
	//Image4f im( "c:/tmp/church_panorama_5097x2889.pfm" );

	im = im.flipUD();

	Array2D< float > data( im.width(), im.height() );
	Array2D< float > output( im.width(), im.height() );

	for( int y = 0; y < im.height(); ++y )
	{
		for( int x = 0; x < im.width(); ++x )
		{
			Vector3f rgb = im.pixel( x, y ).xyz();
            // float lum = ColorUtils::rgb2luminance( rgb );
            // data( x, y ) = lum;
            // jrk: just use red
            data( x, y ) = rgb[0];
		}
	}

	testBilateralFilter( data, 8, 0.1f, output );
	saveArrayAsImage( output, "bf", 8, 0.1f );
	testBilateralFilter( data, 16, 0.1f, output );
	saveArrayAsImage( output, "bf", 16, 0.1f );
	testBilateralFilter( data, 32, 0.2f, output );
	saveArrayAsImage( output, "bf", 32, 0.2f );
	testBilateralFilter( data, 64, 0.4f, output );
	saveArrayAsImage( output, "bf", 64, 0.4f );
    
#if 0
	Image4f edgeImage( "/tmp/step.png" );
	edgeImage.flipUD();

	Array2D< float > edge( im.width(), im.height() );
	for( int y = 0; y < im.height(); ++y )
	{
		for( int x = 0; x < im.width(); ++x )
		{
			edge( x, y ) = edgeImage.pixel( x, y ).x;
		}
	}

	testCrossBilateralFilter( data, edge, 16, 0.1f, output );
	saveArrayAsImage( output, "cbf", 16, 0.1f );
	testCrossBilateralFilter( data, edge, 32, 0.2f, output );
	saveArrayAsImage( output, "cbf", 32, 0.2f );
	testCrossBilateralFilter( data, edge, 64, 0.4f, output );
	saveArrayAsImage( output, "cbf", 64, 0.4f );
#endif

	return 0;
}
