#include <cuda_runtime.h>
// #include <cutil_inline.h>
#include <cutil_standin.h>


#include "BilateralFilter.h"

#include <color/ColorUtils.h>
#include <imageproc/Image4ub.h>
#include <imageproc/Image4f.h>
#include <time/StopWatch.h>
#include <vecmath/Vector3f.h>

#include <QString>

#ifdef CUDA_CUTIL_MISSING
/*static*/ CmdArgReader* CmdArgReader::self;
/*static*/ char** CmdArgReader::rargv;
/*static*/ int CmdArgReader::rargc;

// functions, exported

////////////////////////////////////////////////////////////////////////////////
//! Public construction interface
//! @return a handle to the class instance
//! @param argc number of command line arguments (as given to main())
//! @param argv command line argument string (as given to main())
////////////////////////////////////////////////////////////////////////////////
/*static*/ void
CmdArgReader::init( const int argc, const char** argv) 
{  
    if ( NULL != self) 
    {
        return;
    }

    // command line arguments 
    if (( 0 == argc) || ( 0 == argv)) 
    {
        LOGIC_EXCEPTION( "No command line arguments given.");
    }

    self = new CmdArgReader();

    self->createArgsMaps( argc, argv);

    rargc = argc;
    rargv = const_cast<char**>( argv);
}

////////////////////////////////////////////////////////////////////////////////
//! Constructor, default
////////////////////////////////////////////////////////////////////////////////
CmdArgReader::CmdArgReader() :
    args(),
    unprocessed(),
    iter(),
    iter_unprocessed()
{  }

////////////////////////////////////////////////////////////////////////////////
//! Destructor
////////////////////////////////////////////////////////////////////////////////
CmdArgReader::~CmdArgReader() 
{
    for( iter = args.begin(); iter != args.end(); ++iter) 
    {
        if( *(iter->second.first) == typeid( int)) 
        {
            delete static_cast<int*>( iter->second.second);
            break;
        }
        else if( *(iter->second.first) == typeid( bool)) 
        {
            delete static_cast<bool*>( iter->second.second);
            break;
        }
        else if( *(iter->second.first) == typeid( std::string)) 
        {
            delete static_cast<std::string*>( iter->second.second);
            break;
        }
        else if( *(iter->second.first) == typeid( std::vector< std::string>) ) 
        {
            delete static_cast< std::vector< std::string>* >( iter->second.second);
            break;
        }
        else if( *(iter->second.first) == typeid( std::vector<int>) ) 
        {
            delete static_cast< std::vector<int>* >( iter->second.second);
            break;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
//! Read args as token value pair into map for better processing (Even the 
//! values remain strings until the parameter values is requested by the
//! program.)
//! @param argc the argument count (as given to 'main')
//! @param argv the char* array containing the command line arguments
////////////////////////////////////////////////////////////////////////////////
void
CmdArgReader::createArgsMaps( const int argc, const char** argv) {

    std::string token;
    std::string val_str;

    std::map< std::string, std::string> args;

    std::string::size_type pos;
    std::string arg;
    for( int i=1; i<argc; ++i) 
    {
        arg = argv[i];

        // check if valid command line argument: all arguments begin with - or --
        if (arg[0] != '-') 
        {
            RUNTIME_EXCEPTION("Invalid command line argument.");
        }

        int numDashes = (arg[1] == '-' ? 2 : 1);

        // check if only flag or if a value is given
        if ( (pos = arg.find( '=')) == std::string::npos) 
        {  
            unprocessed[ std::string( arg, numDashes, arg.length()-numDashes)] = "FLAG";                                  
        }
        else 
        {
            unprocessed[ std::string( arg, numDashes, pos-numDashes)] = 
                                      std::string( arg, pos+1, arg.length()-1);
        }
    }
}
#endif // CUDA_CUTIL_MISSING

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
	// int argc2 = 2;
	// char* argv2[2] = { "", "-device=1" };
	if (argc != 2) {
		fprintf(stderr, "Usage: grid <in_image.png>\n");
		exit(0);
	}

	cudaDeviceProp deviceProp;
    int devID = 0; //cutilChooseCudaDevice( argc2, argv2 );
    if( devID < 0 )
	{
       printf( "exiting...\n" );
       cutilExit( argc, argv );
       exit( 0 );
    }
    cutilSafeCall( cudaGetDeviceProperties( &deviceProp, devID ) );

	//Image4f im( "c:/tmp/tulip.png" );
	//Image4f im( "c:/tmp/tulip_1080.png" ); // Jiawen version
	// Image4f im( "../../apps/bilateral_grid/input.png" );
	Image4f im(argv[1]);
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
