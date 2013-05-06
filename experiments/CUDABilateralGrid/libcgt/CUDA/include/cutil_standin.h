#define CUDA_CUTIL_MISSING

/*
* Copyright 1993-2010 NVIDIA Corporation.  All rights reserved.
*
* Please refer to the NVIDIA end user license agreement (EULA) associated
* with this source code for terms and conditions that govern your use of
* this software. Any use, reproduction, disclosure, or distribution of
* this software and related documentation outside the terms of the EULA
* is strictly prohibited.
*
*/

/* CUda UTility Library */
#ifndef _EXCEPTION_H_
#define _EXCEPTION_H_

// includes, system
#include <exception>
#include <stdexcept>
#include <iostream>
#include <stdlib.h>

//! Exception wrapper.
//! @param Std_Exception Exception out of namespace std for easy typing.
template<class Std_Exception>
class Exception : public Std_Exception 
{
public:

    //! @brief Static construction interface
    //! @return Alwayss throws ( Located_Exception<Exception>)
    //! @param file file in which the Exception occurs
    //! @param line line in which the Exception occurs
    //! @param detailed details on the code fragment causing the Exception
    static void throw_it( const char* file, 
                          const int line,
                          const char* detailed = "-" );  

    //! Static construction interface
    //! @return Alwayss throws ( Located_Exception<Exception>)
    //! @param file file in which the Exception occurs
    //! @param line line in which the Exception occurs
    //! @param detailed details on the code fragment causing the Exception
    static void throw_it( const char* file, 
                          const int line,      
                          const std::string& detailed);  

    //! Destructor
    virtual ~Exception() throw(); 

private:

    //! Constructor, default (private)
    Exception(); 

    //! Constructor, standard
    //! @param str string returned by what()
    Exception( const std::string& str); 

};

////////////////////////////////////////////////////////////////////////////////
//! Exception handler function for arbitrary exceptions
//! @param ex exception to handle
////////////////////////////////////////////////////////////////////////////////
template<class Exception_Typ>
inline void
handleException( const Exception_Typ& ex) 
{
    std::cerr << ex.what() << std::endl;

    exit( EXIT_FAILURE);
}

//! Convenience macros

//! Exception caused by dynamic program behavior, e.g. file does not exist
#define RUNTIME_EXCEPTION( msg) \
    Exception<std::runtime_error>::throw_it( __FILE__, __LINE__, msg)

//! Logic exception in program, e.g. an assert failed
#define LOGIC_EXCEPTION( msg) \
    Exception<std::logic_error>::throw_it( __FILE__, __LINE__, msg)

//! Out of range exception
#define RANGE_EXCEPTION( msg) \
    Exception<std::range_error>::throw_it( __FILE__, __LINE__, msg)

////////////////////////////////////////////////////////////////////////////////
//! Implementation

// includes, system
#include <sstream>

////////////////////////////////////////////////////////////////////////////////
//! Static construction interface.
//! @param  Exception causing code fragment (file and line) and detailed infos.
////////////////////////////////////////////////////////////////////////////////
/*static*/ template<class Std_Exception>
void
Exception<Std_Exception>::
throw_it( const char* file, const int line, const char* detailed) 
{
    std::stringstream s;

    // Quiet heavy-weight but exceptions are not for 
    // performance / release versions
    s << "Exception in file '" << file << "' in line " << line << "\n"
      << "Detailed description: " << detailed << "\n";

    throw Exception( s.str());
}

////////////////////////////////////////////////////////////////////////////////
//! Static construction interface.
//! @param  Exception causing code fragment (file and line) and detailed infos.
////////////////////////////////////////////////////////////////////////////////
/*static*/ template<class Std_Exception>
void
Exception<Std_Exception>::
throw_it( const char* file, const int line, const std::string& msg) 
{
    throw_it( file, line, msg.c_str());
}

////////////////////////////////////////////////////////////////////////////////
//! Constructor, default (private).
////////////////////////////////////////////////////////////////////////////////
template<class Std_Exception>
Exception<Std_Exception>::Exception() :
 Exception("Unknown Exception.\n")
{ }

////////////////////////////////////////////////////////////////////////////////
//! Constructor, standard (private).
//! String returned by what().
////////////////////////////////////////////////////////////////////////////////
template<class Std_Exception>
Exception<Std_Exception>::Exception( const std::string& s) :
 Std_Exception( s)
{ }   

////////////////////////////////////////////////////////////////////////////////
//! Destructor
////////////////////////////////////////////////////////////////////////////////
template<class Std_Exception>
Exception<Std_Exception>::~Exception() throw() { }

// functions, exported

#endif // #ifndef _EXCEPTION_H_


/*
 * Copyright 1993-2010 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */
 
 /*
* Copyright 1993-2010 NVIDIA Corporation.  All rights reserved.
*
* Please refer to the NVIDIA end user license agreement (EULA) associated
* with this source code for terms and conditions that govern your use of
* this software. Any use, reproduction, disclosure, or distribution of
* this software and related documentation outside the terms of the EULA
* is strictly prohibited.
*
*/

/* CUda UTility Library */

#ifndef _CMDARGREADER_H_
#define _CMDARGREADER_H_

// includes, system
#include <map>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <typeinfo>

// includes, project
// #include <exception.h>

//! Preprocessed command line arguments
//! @note Lazy evaluation: The arguments are converted from strings to
//!       the correct data type upon request. Converted values are stored 
//!       in an additonal map so that no additional conversion is
//!       necessary. Arrays of command line arguments are stored in 
//!       std::vectors 
//! @note Usage: 
//!          const std::string* file = 
//!                       CmdArgReader::getArg< std::string>( "model")
//!          const std::vector< std::string>* files = 
//!          CmdArgReader::getArg< std::vector< std::string> >( "model")
//! @note All command line arguments begin with '--' followed by the token; 
//!   token and value are seperated by '='; example --samples=50
//! @note Arrays have the form --model=[one.obj,two.obj,three.obj] 
//!       (without whitespaces)

//! Command line argument parser
class CmdArgReader 
{
    template<class> friend class TestCmdArgReader;

protected:

    //! @param self handle to the only instance of this class
    static  CmdArgReader*  self;

public:

    //! Public construction interface
    //! @return a handle to the class instance
    //! @param argc number of command line arguments (as given to main())
    //! @param argv command line argument string (as given to main())
    static void  init( const int argc, const char** argv);

public:

    //! Get the value of the command line argument with given name
    //! @return A const handle to the requested argument.
    //! If the argument does not exist or if it 
    //!  is not from type T NULL is returned
    //! @param name the name of the requested argument
    //! @note T the type of the argument requested
    template<class T>
        static inline const T* getArg( const std::string& name);

    //! Check if a command line argument with the given name exists
    //! @return  true if a command line argument with name \a name exists,
    //!          otherwise false
    //! @param name  name of the command line argument in question
    static inline bool existArg( const std::string& name);

    //! Get the original / raw argc program argument
    static inline int& getRArgc();

    //! Get the original / raw argv program argument
    static inline char**& getRArgv();

public:

    //! Destructor
    ~CmdArgReader();

protected:

    //! Constructor, default
    CmdArgReader();

private:

    // private helper functions

    //! Get the value of the command line argument with given name
    //! @note Private helper function for 'getArg' to work on the members
    //! @return A const handle to the requested argument. If the argument
    //!         does not exist or if it  is not from type T a NULL pointer
    //!         is returned.
    //! @param name the name of the requested argument
    //! @note T the type of the argument requested
    template<class T>
        inline const T* getArgHelper( const std::string& name);

    //! Check if a command line argument with name \a name exists
    //! @return true if a command line argument of name \a name exists, 
    //!         otherwise false
    //! @param name the name of the requested argument
    inline bool existArgHelper( const std::string& name) const;

    //! Read args as token value pair into map for better processing
    //!  (Even the values remain strings until the parameter values is 
    //!   requested by the program.)
    //! @param argc the argument count (as given to 'main')
    //! @param argv the char* array containing the command line arguments
    void  createArgsMaps( const int argc, const char** argv);

    //! Helper for "casting" the strings from the map with the unprocessed 
    //! values to the correct 
    //!  data type.
    //! @return true if conversion succeeded, otherwise false
    //! @param element the value as string
    //! @param val the value as type T
    template<class T>
        static inline bool convertToT( const std::string& element, T& val);

public:

    // typedefs internal

    //! container for a processed command line argument
    //! typeid is used to easily be able to decide if a re-requested token-value
    //! pair match the type of the first conversion
    typedef std::pair< const std::type_info*, void*>  ValType;
    //! map of already converted values
    typedef std::map< std::string, ValType >          ArgsMap;
    //! iterator for the map of already converted values
    typedef ArgsMap::iterator                         ArgsMapIter;
    typedef ArgsMap::const_iterator                   ConstArgsMapIter;

    //! map of unprocessed (means unconverted) token-value pairs
    typedef std::map< std::string, std::string>            UnpMap;
    //! iterator for the map of unprocessed (means unconverted) token-value pairs
    typedef std::map< std::string, std::string>::iterator  UnpMapIter;

private:

#ifdef _WIN32
#  pragma warning( disable: 4251)
#endif

    //! rargc original value of argc
    static  int  rargc;

    //! rargv contains command line arguments in raw format
    static char**  rargv;

    //! args Map containing the already converted token-value pairs
    ArgsMap     args;

    //! args Map containing the unprocessed / unconverted token-value pairs
    UnpMap     unprocessed;

    //! iter Iterator for the map with the already converted token-value 
    //!  pairs (to avoid frequent reallocation)
    ArgsMapIter iter;

    //! iter Iterator for the map with the unconverted token-value 
    //!  pairs (to avoid frequent reallocation)
    UnpMapIter iter_unprocessed;

#ifdef _WIN32
#  pragma warning( default: 4251)
#endif

private:

    //! Constructor, copy (not implemented)
    CmdArgReader( const CmdArgReader&);

    //! Assignment operator (not implemented)
    CmdArgReader& operator=( const CmdArgReader&);
};

// variables, exported (extern)

// functions, inlined (inline)

////////////////////////////////////////////////////////////////////////////////
//! Conversion function for command line argument arrays
//! @note This function is used each type for which no template specialization
//!  exist (which will cause errors if the type does not fulfill the std::vector
//!  interface).
////////////////////////////////////////////////////////////////////////////////
template<class T>
/*static*/ inline bool
CmdArgReader::convertToT( const std::string& element, T& val)
{
    // preallocate storage
    val.resize( std::count( element.begin(), element.end(), ',') + 1);

    unsigned int i = 0;
    std::string::size_type pos_start = 1;  // leave array prefix '['
    std::string::size_type pos_end = 0;

    // do for all elements of the comma seperated list
    while( std::string::npos != ( pos_end = element.find(',', pos_end+1)) ) 
    {
        // convert each element by the appropriate function
        if ( ! convertToT< typename T::value_type >( 
            std::string( element, pos_start, pos_end - pos_start), val[i])) 
        {
            return false;
        }

        pos_start = pos_end + 1;
        ++i;
    }

    std::string tmp1(  element, pos_start, element.length() - pos_start - 1);

    // process last element (leave array postfix ']')
    if ( ! convertToT< typename T::value_type >( std::string( element,
        pos_start,
        element.length() - pos_start - 1),
        val[i])) 
    {
        return false;
    }

    // possible to process all elements?
    return true;
}

////////////////////////////////////////////////////////////////////////////////
//! Conversion function for command line arguments of type int
////////////////////////////////////////////////////////////////////////////////
template<>
inline bool
CmdArgReader::convertToT<int>( const std::string& element, int& val) 
{
    std::istringstream ios( element);
    ios >> val;

    bool ret_val = false;
    if ( ios.eof()) 
    {
        ret_val = true;
    }

    return ret_val;
}

////////////////////////////////////////////////////////////////////////////////
//! Conversion function for command line arguments of type float
////////////////////////////////////////////////////////////////////////////////
template<>
inline bool
CmdArgReader::convertToT<float>( const std::string& element, float& val) 
{
    std::istringstream ios( element);
    ios >> val;

    bool ret_val = false;
    if ( ios.eof()) 
    {
        ret_val = true;
    }

    return ret_val;
}

////////////////////////////////////////////////////////////////////////////////
//! Conversion function for command line arguments of type double
////////////////////////////////////////////////////////////////////////////////
template<>
inline bool
CmdArgReader::convertToT<double>( const std::string& element, double& val) 
{
    std::istringstream ios( element);
    ios >> val;

    bool ret_val = false;
    if ( ios.eof()) 
    {
        ret_val = true;
    }

    return ret_val;
}

////////////////////////////////////////////////////////////////////////////////
//! Conversion function for command line arguments of type string
////////////////////////////////////////////////////////////////////////////////
template<>
inline bool
CmdArgReader::convertToT<std::string>( const std::string& element, 
                                      std::string& val)
{
    val = element;
    return true;
}

////////////////////////////////////////////////////////////////////////////////
//! Conversion function for command line arguments of type bool
////////////////////////////////////////////////////////////////////////////////
template<>
inline bool
CmdArgReader::convertToT<bool>( const std::string& element, bool& val) 
{
    // check if value is given as string-type { true | false }
    if ( "true" == element) 
    {
        val = true;
        return true;
    }
    else if ( "false" == element) 
    {
        val = false;
        return true;
    }
    // check if argument is given as integer { 0 | 1 }
    else 
    {
        int tmp;
        if ( convertToT<int>( element, tmp)) 
        {
            if ( 1 == tmp) 
            {
                val = true;
                return true;
            }
            else if ( 0 == tmp) 
            {
                val = false;
                return true;
            }
        }
    }

    return false;
}

////////////////////////////////////////////////////////////////////////////////
//! Get the value of the command line argument with given name
//! @return A const handle to the requested argument. If the argument does
//!  not exist or if it is not from type T NULL is returned
//! @param T the type of the argument requested
//! @param name the name of the requested argument
////////////////////////////////////////////////////////////////////////////////
template<class T>
/*static*/ const T*
CmdArgReader::getArg( const std::string& name) 
{
    if( ! self) 
    {
        RUNTIME_EXCEPTION("CmdArgReader::getArg(): CmdArgReader not initialized.");
        return NULL;
    }

    return self->getArgHelper<T>( name);
}

////////////////////////////////////////////////////////////////////////////////
//! Check if a command line argument with the given name exists
//! @return  true if a command line argument with name \a name exists,
//!          otherwise false
//! @param name  name of the command line argument in question
////////////////////////////////////////////////////////////////////////////////
/*static*/ inline bool 
CmdArgReader::existArg( const std::string& name) 
{
    if( ! self) 
    {
        RUNTIME_EXCEPTION("CmdArgReader::getArg(): CmdArgReader not initialized.");
        return false;
    }

    return self->existArgHelper( name);
}

////////////////////////////////////////////////////////////////////////////////
//! @brief Get the value of the command line argument with given name
//! @return A const handle to the requested argument. If the argument does
//!  not exist or if it is not from type T NULL is returned
//! @param T the type of the argument requested
//! @param name the name of the requested argument
////////////////////////////////////////////////////////////////////////////////
template<class T>
const T*
CmdArgReader::getArgHelper( const std::string& name) 
{
    // check if argument already processed and stored in correct type
    if ( args.end() != (iter = args.find( name))) 
    {
        if ( (*(iter->second.first)) == typeid( T) ) 
        {
            return (T*) iter->second.second;
        }
    }
    else 
    {
        T* tmp = new T;

        // check the array with unprocessed values
        if ( unprocessed.end() != (iter_unprocessed = unprocessed.find( name))) 
        {
            // try to "cast" the string to the type requested
            if ( convertToT< T >( iter_unprocessed->second, *tmp)) 
            {
                // add the token element pair to map of already converted values
                args[name] = std::make_pair( &(typeid( T)), (void*) tmp);

                return tmp;
            }
        }

        // not used while not inserted into the map -> cleanup
        delete tmp;
    }

    // failed, argument not available
    return NULL;
}

////////////////////////////////////////////////////////////////////////////////
//! Check if a command line argument with name \a name exists
//! @return true if a command line argument of name \a name exists, 
//!         otherwise false
//! @param name the name of the requested argument
////////////////////////////////////////////////////////////////////////////////
inline bool
CmdArgReader::existArgHelper( const std::string& name) const 
{
    bool ret_val = false;

    // check if argument already processed and stored in correct type
    if( args.end() != args.find( name)) 
    {
        ret_val = true;
    }
    else 
    {

        // check the array with unprocessed values
        if ( unprocessed.end() != unprocessed.find( name)) 
        {
            ret_val = true; 
        }
    }

    return ret_val;
}

////////////////////////////////////////////////////////////////////////////////
//! Get the original / raw argc program argument
////////////////////////////////////////////////////////////////////////////////
/*static*/ inline int&
CmdArgReader::getRArgc() 
{
    if( ! self) 
    {
        RUNTIME_EXCEPTION("CmdArgReader::getRArgc(): CmdArgReader not initialized.");
    }

    return rargc;
}

////////////////////////////////////////////////////////////////////////////////
//! Get the original / raw argv program argument
////////////////////////////////////////////////////////////////////////////////
/*static*/ inline char**&
CmdArgReader::getRArgv() 
{
    if( ! self) 
    {
        RUNTIME_EXCEPTION("CmdArgReader::getRArgc(): CmdArgReader not initialized.");
    }

    return rargv;
}

// functions, exported (extern)

#endif // #ifndef _CMDARGREADER_H_


#ifndef CUTIL_STANDIN_H
#define CUTIL_STANDIN_H

#  define CUDA_SAFE_CALL_NO_SYNC( call) {                                    \
    cudaError err = call;                                                    \
    if( cudaSuccess != err) {                                                \
        fprintf(stderr, "Cuda error in file '%s' in line %i : %s.\n",        \
                __FILE__, __LINE__, cudaGetErrorString( err) );              \
        exit(EXIT_FAILURE);                                                  \
    } }

#  define CUDA_SAFE_CALL( call)     CUDA_SAFE_CALL_NO_SYNC(call);                                            \

enum CUTBoolean 
{
CUTFalse = 0,
CUTTrue = 1
};

////////////////////////////////////////////////////////////////////////////////
//! Check if command line argument \a flag-name is given
//! @return 1 if command line argument \a flag_name has been given, otherwise 0
//! @param argc  argc as passed to main()
//! @param argv  argv as passed to main()
//! @param flag_name  name of command line flag
////////////////////////////////////////////////////////////////////////////////
inline CUTBoolean
cutCheckCmdLineFlag( const int argc, const char** argv, const char* flag_name) 
{
    CUTBoolean ret_val = CUTFalse;

    try 
    {
        // initalize 
        CmdArgReader::init( argc, argv);

        // check if the command line argument exists
        if( CmdArgReader::existArg( flag_name)) 
        {
            ret_val = CUTTrue;
        }
    }
    catch( const std::exception& /*ex*/) 
    {    
        std::cerr << "Error when parsing command line argument string." << std::endl;
    } 

    return ret_val;
}

////////////////////////////////////////////////////////////////////////////////
//! Get the value of a command line argument of type int
//! @return CUTTrue if command line argument \a arg_name has been given and
//!         is of the requested type, otherwise CUTFalse
//! @param argc  argc as passed to main()
//! @param argv  argv as passed to main()
//! @param arg_name  name of the command line argument
//! @param val  value of the command line argument
////////////////////////////////////////////////////////////////////////////////
inline CUTBoolean
cutGetCmdLineArgumenti( const int argc, const char** argv, 
                        const char* arg_name, int* val) 
{
    CUTBoolean ret_val = CUTFalse;

    try 
    {
        // initialize
        CmdArgReader::init( argc, argv);

        // access argument
        const int* v = CmdArgReader::getArg<int>( arg_name);
        if( NULL != v) 
        {
            // assign value
            *val = *v;
            ret_val = CUTTrue;
        }		
		else {
			// fail safe
			val = NULL;
		}
    }
    catch( const std::exception& /*ex*/) 
    {    
        std::cerr << "Error when parsing command line argument string." << std::endl;
    } 

    return ret_val;
}

#endif

// copied over from cutil_math.h, in old CUDA 4.x SDK samples.
/*
 * Copyright 1993-2010 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */

/*
    This file implements common mathematical operations on vector types
    (float3, float4 etc.) since these are not provided as standard by CUDA.

    The syntax is modelled on the Cg standard library.

    This is part of the CUTIL library and is not supported by NVIDIA.

    Thanks to Linh Hah for additions and fixes.
*/

#ifndef CUTIL_MATH_H
#define CUTIL_MATH_H

#include <cuda_runtime.h>
#include <vector_functions.h>

typedef unsigned int uint;
typedef unsigned short ushort;

#ifndef __CUDACC__
#include <stdlib.h>
#include <math.h>

////////////////////////////////////////////////////////////////////////////////
// host implementations of CUDA functions
////////////////////////////////////////////////////////////////////////////////

inline float fminf(float a, float b)
{
  return a < b ? a : b;
}

inline float fmaxf(float a, float b)
{
  return a > b ? a : b;
}

inline int max(int a, int b)
{
  return a > b ? a : b;
}

inline int min(int a, int b)
{
  return a < b ? a : b;
}

inline float rsqrtf(float x)
{
    return 1.0f / sqrtf(x);
}
#endif

////////////////////////////////////////////////////////////////////////////////
// constructors
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float2 make_float2(float s)
{
    return make_float2(s, s);
}
inline __host__ __device__ float2 make_float2(float3 a)
{
    return make_float2(a.x, a.y);
}
inline __host__ __device__ float2 make_float2(int2 a)
{
    return make_float2(float(a.x), float(a.y));
}
inline __host__ __device__ float2 make_float2(uint2 a)
{
    return make_float2(float(a.x), float(a.y));
}

inline __host__ __device__ int2 make_int2(int s)
{
    return make_int2(s, s);
}
inline __host__ __device__ int2 make_int2(int3 a)
{
    return make_int2(a.x, a.y);
}
inline __host__ __device__ int2 make_int2(uint2 a)
{
    return make_int2(int(a.x), int(a.y));
}
inline __host__ __device__ int2 make_int2(float2 a)
{
    return make_int2(int(a.x), int(a.y));
}

inline __host__ __device__ uint2 make_uint2(uint s)
{
    return make_uint2(s, s);
}
inline __host__ __device__ uint2 make_uint2(uint3 a)
{
    return make_uint2(a.x, a.y);
}
inline __host__ __device__ uint2 make_uint2(int2 a)
{
    return make_uint2(uint(a.x), uint(a.y));
}

inline __host__ __device__ float3 make_float3(float s)
{
    return make_float3(s, s, s);
}
inline __host__ __device__ float3 make_float3(float2 a)
{
    return make_float3(a.x, a.y, 0.0f);
}
inline __host__ __device__ float3 make_float3(float2 a, float s)
{
    return make_float3(a.x, a.y, s);
}
inline __host__ __device__ float3 make_float3(float4 a)
{
    return make_float3(a.x, a.y, a.z);
}
inline __host__ __device__ float3 make_float3(int3 a)
{
    return make_float3(float(a.x), float(a.y), float(a.z));
}
inline __host__ __device__ float3 make_float3(uint3 a)
{
    return make_float3(float(a.x), float(a.y), float(a.z));
}

inline __host__ __device__ int3 make_int3(int s)
{
    return make_int3(s, s, s);
}
inline __host__ __device__ int3 make_int3(int2 a)
{
    return make_int3(a.x, a.y, 0);
}
inline __host__ __device__ int3 make_int3(int2 a, int s)
{
    return make_int3(a.x, a.y, s);
}
inline __host__ __device__ int3 make_int3(uint3 a)
{
    return make_int3(int(a.x), int(a.y), int(a.z));
}
inline __host__ __device__ int3 make_int3(float3 a)
{
    return make_int3(int(a.x), int(a.y), int(a.z));
}

inline __host__ __device__ uint3 make_uint3(uint s)
{
    return make_uint3(s, s, s);
}
inline __host__ __device__ uint3 make_uint3(uint2 a)
{
    return make_uint3(a.x, a.y, 0);
}
inline __host__ __device__ uint3 make_uint3(uint2 a, uint s)
{
    return make_uint3(a.x, a.y, s);
}
inline __host__ __device__ uint3 make_uint3(uint4 a)
{
    return make_uint3(a.x, a.y, a.z);
}
inline __host__ __device__ uint3 make_uint3(int3 a)
{
    return make_uint3(uint(a.x), uint(a.y), uint(a.z));
}

inline __host__ __device__ float4 make_float4(float s)
{
    return make_float4(s, s, s, s);
}
inline __host__ __device__ float4 make_float4(float3 a)
{
    return make_float4(a.x, a.y, a.z, 0.0f);
}
inline __host__ __device__ float4 make_float4(float3 a, float w)
{
    return make_float4(a.x, a.y, a.z, w);
}
inline __host__ __device__ float4 make_float4(int4 a)
{
    return make_float4(float(a.x), float(a.y), float(a.z), float(a.w));
}
inline __host__ __device__ float4 make_float4(uint4 a)
{
    return make_float4(float(a.x), float(a.y), float(a.z), float(a.w));
}

inline __host__ __device__ int4 make_int4(int s)
{
    return make_int4(s, s, s, s);
}
inline __host__ __device__ int4 make_int4(int3 a)
{
    return make_int4(a.x, a.y, a.z, 0);
}
inline __host__ __device__ int4 make_int4(int3 a, int w)
{
    return make_int4(a.x, a.y, a.z, w);
}
inline __host__ __device__ int4 make_int4(uint4 a)
{
    return make_int4(int(a.x), int(a.y), int(a.z), int(a.w));
}
inline __host__ __device__ int4 make_int4(float4 a)
{
    return make_int4(int(a.x), int(a.y), int(a.z), int(a.w));
}


inline __host__ __device__ uint4 make_uint4(uint s)
{
    return make_uint4(s, s, s, s);
}
inline __host__ __device__ uint4 make_uint4(uint3 a)
{
    return make_uint4(a.x, a.y, a.z, 0);
}
inline __host__ __device__ uint4 make_uint4(uint3 a, uint w)
{
    return make_uint4(a.x, a.y, a.z, w);
}
inline __host__ __device__ uint4 make_uint4(int4 a)
{
    return make_uint4(uint(a.x), uint(a.y), uint(a.z), uint(a.w));
}

////////////////////////////////////////////////////////////////////////////////
// negate
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float2 operator-(float2 &a)
{
    return make_float2(-a.x, -a.y);
}
inline __host__ __device__ int2 operator-(int2 &a)
{
    return make_int2(-a.x, -a.y);
}
inline __host__ __device__ float3 operator-(float3 &a)
{
    return make_float3(-a.x, -a.y, -a.z);
}
inline __host__ __device__ int3 operator-(int3 &a)
{
    return make_int3(-a.x, -a.y, -a.z);
}
inline __host__ __device__ float4 operator-(float4 &a)
{
    return make_float4(-a.x, -a.y, -a.z, -a.w);
}
inline __host__ __device__ int4 operator-(int4 &a)
{
    return make_int4(-a.x, -a.y, -a.z, -a.w);
}

////////////////////////////////////////////////////////////////////////////////
// addition
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float2 operator+(float2 a, float2 b)
{
    return make_float2(a.x + b.x, a.y + b.y);
}
inline __host__ __device__ void operator+=(float2 &a, float2 b)
{
    a.x += b.x; a.y += b.y;
}
inline __host__ __device__ float2 operator+(float2 a, float b)
{
    return make_float2(a.x + b, a.y + b);
}
inline __host__ __device__ float2 operator+(float b, float2 a)
{
    return make_float2(a.x + b, a.y + b);
}
inline __host__ __device__ void operator+=(float2 &a, float b)
{
    a.x += b; a.y += b;
}

inline __host__ __device__ int2 operator+(int2 a, int2 b)
{
    return make_int2(a.x + b.x, a.y + b.y);
}
inline __host__ __device__ void operator+=(int2 &a, int2 b)
{
    a.x += b.x; a.y += b.y;
}
inline __host__ __device__ int2 operator+(int2 a, int b)
{
    return make_int2(a.x + b, a.y + b);
}
inline __host__ __device__ int2 operator+(int b, int2 a)
{
    return make_int2(a.x + b, a.y + b);
}
inline __host__ __device__ void operator+=(int2 &a, int b)
{
    a.x += b; a.y += b;
}

inline __host__ __device__ uint2 operator+(uint2 a, uint2 b)
{
    return make_uint2(a.x + b.x, a.y + b.y);
}
inline __host__ __device__ void operator+=(uint2 &a, uint2 b)
{
    a.x += b.x; a.y += b.y;
}
inline __host__ __device__ uint2 operator+(uint2 a, uint b)
{
    return make_uint2(a.x + b, a.y + b);
}
inline __host__ __device__ uint2 operator+(uint b, uint2 a)
{
    return make_uint2(a.x + b, a.y + b);
}
inline __host__ __device__ void operator+=(uint2 &a, uint b)
{
    a.x += b; a.y += b;
}


inline __host__ __device__ float3 operator+(float3 a, float3 b)
{
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}
inline __host__ __device__ void operator+=(float3 &a, float3 b)
{
    a.x += b.x; a.y += b.y; a.z += b.z;
}
inline __host__ __device__ float3 operator+(float3 a, float b)
{
    return make_float3(a.x + b, a.y + b, a.z + b);
}
inline __host__ __device__ void operator+=(float3 &a, float b)
{
    a.x += b; a.y += b; a.z += b;
}

inline __host__ __device__ int3 operator+(int3 a, int3 b)
{
    return make_int3(a.x + b.x, a.y + b.y, a.z + b.z);
}
inline __host__ __device__ void operator+=(int3 &a, int3 b)
{
    a.x += b.x; a.y += b.y; a.z += b.z;
}
inline __host__ __device__ int3 operator+(int3 a, int b)
{
    return make_int3(a.x + b, a.y + b, a.z + b);
}
inline __host__ __device__ void operator+=(int3 &a, int b)
{
    a.x += b; a.y += b; a.z += b;
}

inline __host__ __device__ uint3 operator+(uint3 a, uint3 b)
{
    return make_uint3(a.x + b.x, a.y + b.y, a.z + b.z);
}
inline __host__ __device__ void operator+=(uint3 &a, uint3 b)
{
    a.x += b.x; a.y += b.y; a.z += b.z;
}
inline __host__ __device__ uint3 operator+(uint3 a, uint b)
{
    return make_uint3(a.x + b, a.y + b, a.z + b);
}
inline __host__ __device__ void operator+=(uint3 &a, uint b)
{
    a.x += b; a.y += b; a.z += b;
}

inline __host__ __device__ int3 operator+(int b, int3 a)
{
    return make_int3(a.x + b, a.y + b, a.z + b);
}
inline __host__ __device__ uint3 operator+(uint b, uint3 a)
{
    return make_uint3(a.x + b, a.y + b, a.z + b);
}
inline __host__ __device__ float3 operator+(float b, float3 a)
{
    return make_float3(a.x + b, a.y + b, a.z + b);
}

inline __host__ __device__ float4 operator+(float4 a, float4 b)
{
    return make_float4(a.x + b.x, a.y + b.y, a.z + b.z,  a.w + b.w);
}
inline __host__ __device__ void operator+=(float4 &a, float4 b)
{
    a.x += b.x; a.y += b.y; a.z += b.z; a.w += b.w;
}
inline __host__ __device__ float4 operator+(float4 a, float b)
{
    return make_float4(a.x + b, a.y + b, a.z + b, a.w + b);
}
inline __host__ __device__ float4 operator+(float b, float4 a)
{
    return make_float4(a.x + b, a.y + b, a.z + b, a.w + b);
}
inline __host__ __device__ void operator+=(float4 &a, float b)
{
    a.x += b; a.y += b; a.z += b; a.w += b;
}

inline __host__ __device__ int4 operator+(int4 a, int4 b)
{
    return make_int4(a.x + b.x, a.y + b.y, a.z + b.z,  a.w + b.w);
}
inline __host__ __device__ void operator+=(int4 &a, int4 b)
{
    a.x += b.x; a.y += b.y; a.z += b.z; a.w += b.w;
}
inline __host__ __device__ int4 operator+(int4 a, int b)
{
    return make_int4(a.x + b, a.y + b, a.z + b,  a.w + b);
}
inline __host__ __device__ int4 operator+(int b, int4 a)
{
    return make_int4(a.x + b, a.y + b, a.z + b,  a.w + b);
}
inline __host__ __device__ void operator+=(int4 &a, int b)
{
    a.x += b; a.y += b; a.z += b; a.w += b;
}

inline __host__ __device__ uint4 operator+(uint4 a, uint4 b)
{
    return make_uint4(a.x + b.x, a.y + b.y, a.z + b.z,  a.w + b.w);
}
inline __host__ __device__ void operator+=(uint4 &a, uint4 b)
{
    a.x += b.x; a.y += b.y; a.z += b.z; a.w += b.w;
}
inline __host__ __device__ uint4 operator+(uint4 a, uint b)
{
    return make_uint4(a.x + b, a.y + b, a.z + b,  a.w + b);
}
inline __host__ __device__ uint4 operator+(uint b, uint4 a)
{
    return make_uint4(a.x + b, a.y + b, a.z + b,  a.w + b);
}
inline __host__ __device__ void operator+=(uint4 &a, uint b)
{
    a.x += b; a.y += b; a.z += b; a.w += b;
}

////////////////////////////////////////////////////////////////////////////////
// subtract
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float2 operator-(float2 a, float2 b)
{
    return make_float2(a.x - b.x, a.y - b.y);
}
inline __host__ __device__ void operator-=(float2 &a, float2 b)
{
    a.x -= b.x; a.y -= b.y;
}
inline __host__ __device__ float2 operator-(float2 a, float b)
{
    return make_float2(a.x - b, a.y - b);
}
inline __host__ __device__ float2 operator-(float b, float2 a)
{
    return make_float2(b - a.x, b - a.y);
}
inline __host__ __device__ void operator-=(float2 &a, float b)
{
    a.x -= b; a.y -= b;
}

inline __host__ __device__ int2 operator-(int2 a, int2 b)
{
    return make_int2(a.x - b.x, a.y - b.y);
}
inline __host__ __device__ void operator-=(int2 &a, int2 b)
{
    a.x -= b.x; a.y -= b.y;
}
inline __host__ __device__ int2 operator-(int2 a, int b)
{
    return make_int2(a.x - b, a.y - b);
}
inline __host__ __device__ int2 operator-(int b, int2 a)
{
    return make_int2(b - a.x, b - a.y);
}
inline __host__ __device__ void operator-=(int2 &a, int b)
{
    a.x -= b; a.y -= b;
}

inline __host__ __device__ uint2 operator-(uint2 a, uint2 b)
{
    return make_uint2(a.x - b.x, a.y - b.y);
}
inline __host__ __device__ void operator-=(uint2 &a, uint2 b)
{
    a.x -= b.x; a.y -= b.y;
}
inline __host__ __device__ uint2 operator-(uint2 a, uint b)
{
    return make_uint2(a.x - b, a.y - b);
}
inline __host__ __device__ uint2 operator-(uint b, uint2 a)
{
    return make_uint2(b - a.x, b - a.y);
}
inline __host__ __device__ void operator-=(uint2 &a, uint b)
{
    a.x -= b; a.y -= b;
}

inline __host__ __device__ float3 operator-(float3 a, float3 b)
{
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}
inline __host__ __device__ void operator-=(float3 &a, float3 b)
{
    a.x -= b.x; a.y -= b.y; a.z -= b.z;
}
inline __host__ __device__ float3 operator-(float3 a, float b)
{
    return make_float3(a.x - b, a.y - b, a.z - b);
}
inline __host__ __device__ float3 operator-(float b, float3 a)
{
    return make_float3(b - a.x, b - a.y, b - a.z);
}
inline __host__ __device__ void operator-=(float3 &a, float b)
{
    a.x -= b; a.y -= b; a.z -= b;
}

inline __host__ __device__ int3 operator-(int3 a, int3 b)
{
    return make_int3(a.x - b.x, a.y - b.y, a.z - b.z);
}
inline __host__ __device__ void operator-=(int3 &a, int3 b)
{
    a.x -= b.x; a.y -= b.y; a.z -= b.z;
}
inline __host__ __device__ int3 operator-(int3 a, int b)
{
    return make_int3(a.x - b, a.y - b, a.z - b);
}
inline __host__ __device__ int3 operator-(int b, int3 a)
{
    return make_int3(b - a.x, b - a.y, b - a.z);
}
inline __host__ __device__ void operator-=(int3 &a, int b)
{
    a.x -= b; a.y -= b; a.z -= b;
}

inline __host__ __device__ uint3 operator-(uint3 a, uint3 b)
{
    return make_uint3(a.x - b.x, a.y - b.y, a.z - b.z);
}
inline __host__ __device__ void operator-=(uint3 &a, uint3 b)
{
    a.x -= b.x; a.y -= b.y; a.z -= b.z;
}
inline __host__ __device__ uint3 operator-(uint3 a, uint b)
{
    return make_uint3(a.x - b, a.y - b, a.z - b);
}
inline __host__ __device__ uint3 operator-(uint b, uint3 a)
{
    return make_uint3(b - a.x, b - a.y, b - a.z);
}
inline __host__ __device__ void operator-=(uint3 &a, uint b)
{
    a.x -= b; a.y -= b; a.z -= b;
}

inline __host__ __device__ float4 operator-(float4 a, float4 b)
{
    return make_float4(a.x - b.x, a.y - b.y, a.z - b.z,  a.w - b.w);
}
inline __host__ __device__ void operator-=(float4 &a, float4 b)
{
    a.x -= b.x; a.y -= b.y; a.z -= b.z; a.w -= b.w;
}
inline __host__ __device__ float4 operator-(float4 a, float b)
{
    return make_float4(a.x - b, a.y - b, a.z - b,  a.w - b);
}
inline __host__ __device__ void operator-=(float4 &a, float b)
{
    a.x -= b; a.y -= b; a.z -= b; a.w -= b;
}

inline __host__ __device__ int4 operator-(int4 a, int4 b)
{
    return make_int4(a.x - b.x, a.y - b.y, a.z - b.z,  a.w - b.w);
}
inline __host__ __device__ void operator-=(int4 &a, int4 b)
{
    a.x -= b.x; a.y -= b.y; a.z -= b.z; a.w -= b.w;
}
inline __host__ __device__ int4 operator-(int4 a, int b)
{
    return make_int4(a.x - b, a.y - b, a.z - b,  a.w - b);
}
inline __host__ __device__ int4 operator-(int b, int4 a)
{
    return make_int4(b - a.x, b - a.y, b - a.z, b - a.w);
}
inline __host__ __device__ void operator-=(int4 &a, int b)
{
    a.x -= b; a.y -= b; a.z -= b; a.w -= b;
}

inline __host__ __device__ uint4 operator-(uint4 a, uint4 b)
{
    return make_uint4(a.x - b.x, a.y - b.y, a.z - b.z,  a.w - b.w);
}
inline __host__ __device__ void operator-=(uint4 &a, uint4 b)
{
    a.x -= b.x; a.y -= b.y; a.z -= b.z; a.w -= b.w;
}
inline __host__ __device__ uint4 operator-(uint4 a, uint b)
{
    return make_uint4(a.x - b, a.y - b, a.z - b,  a.w - b);
}
inline __host__ __device__ uint4 operator-(uint b, uint4 a)
{
    return make_uint4(b - a.x, b - a.y, b - a.z, b - a.w);
}
inline __host__ __device__ void operator-=(uint4 &a, uint b)
{
    a.x -= b; a.y -= b; a.z -= b; a.w -= b;
}

////////////////////////////////////////////////////////////////////////////////
// multiply
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float2 operator*(float2 a, float2 b)
{
    return make_float2(a.x * b.x, a.y * b.y);
}
inline __host__ __device__ void operator*=(float2 &a, float2 b)
{
    a.x *= b.x; a.y *= b.y;
}
inline __host__ __device__ float2 operator*(float2 a, float b)
{
    return make_float2(a.x * b, a.y * b);
}
inline __host__ __device__ float2 operator*(float b, float2 a)
{
    return make_float2(b * a.x, b * a.y);
}
inline __host__ __device__ void operator*=(float2 &a, float b)
{
    a.x *= b; a.y *= b;
}

inline __host__ __device__ int2 operator*(int2 a, int2 b)
{
    return make_int2(a.x * b.x, a.y * b.y);
}
inline __host__ __device__ void operator*=(int2 &a, int2 b)
{
    a.x *= b.x; a.y *= b.y;
}
inline __host__ __device__ int2 operator*(int2 a, int b)
{
    return make_int2(a.x * b, a.y * b);
}
inline __host__ __device__ int2 operator*(int b, int2 a)
{
    return make_int2(b * a.x, b * a.y);
}
inline __host__ __device__ void operator*=(int2 &a, int b)
{
    a.x *= b; a.y *= b;
}

inline __host__ __device__ uint2 operator*(uint2 a, uint2 b)
{
    return make_uint2(a.x * b.x, a.y * b.y);
}
inline __host__ __device__ void operator*=(uint2 &a, uint2 b)
{
    a.x *= b.x; a.y *= b.y;
}
inline __host__ __device__ uint2 operator*(uint2 a, uint b)
{
    return make_uint2(a.x * b, a.y * b);
}
inline __host__ __device__ uint2 operator*(uint b, uint2 a)
{
    return make_uint2(b * a.x, b * a.y);
}
inline __host__ __device__ void operator*=(uint2 &a, uint b)
{
    a.x *= b; a.y *= b;
}

inline __host__ __device__ float3 operator*(float3 a, float3 b)
{
    return make_float3(a.x * b.x, a.y * b.y, a.z * b.z);
}
inline __host__ __device__ void operator*=(float3 &a, float3 b)
{
    a.x *= b.x; a.y *= b.y; a.z *= b.z;
}
inline __host__ __device__ float3 operator*(float3 a, float b)
{
    return make_float3(a.x * b, a.y * b, a.z * b);
}
inline __host__ __device__ float3 operator*(float b, float3 a)
{
    return make_float3(b * a.x, b * a.y, b * a.z);
}
inline __host__ __device__ void operator*=(float3 &a, float b)
{
    a.x *= b; a.y *= b; a.z *= b;
}

inline __host__ __device__ int3 operator*(int3 a, int3 b)
{
    return make_int3(a.x * b.x, a.y * b.y, a.z * b.z);
}
inline __host__ __device__ void operator*=(int3 &a, int3 b)
{
    a.x *= b.x; a.y *= b.y; a.z *= b.z;
}
inline __host__ __device__ int3 operator*(int3 a, int b)
{
    return make_int3(a.x * b, a.y * b, a.z * b);
}
inline __host__ __device__ int3 operator*(int b, int3 a)
{
    return make_int3(b * a.x, b * a.y, b * a.z);
}
inline __host__ __device__ void operator*=(int3 &a, int b)
{
    a.x *= b; a.y *= b; a.z *= b;
}

inline __host__ __device__ uint3 operator*(uint3 a, uint3 b)
{
    return make_uint3(a.x * b.x, a.y * b.y, a.z * b.z);
}
inline __host__ __device__ void operator*=(uint3 &a, uint3 b)
{
    a.x *= b.x; a.y *= b.y; a.z *= b.z;
}
inline __host__ __device__ uint3 operator*(uint3 a, uint b)
{
    return make_uint3(a.x * b, a.y * b, a.z * b);
}
inline __host__ __device__ uint3 operator*(uint b, uint3 a)
{
    return make_uint3(b * a.x, b * a.y, b * a.z);
}
inline __host__ __device__ void operator*=(uint3 &a, uint b)
{
    a.x *= b; a.y *= b; a.z *= b;
}

inline __host__ __device__ float4 operator*(float4 a, float4 b)
{
    return make_float4(a.x * b.x, a.y * b.y, a.z * b.z,  a.w * b.w);
}
inline __host__ __device__ void operator*=(float4 &a, float4 b)
{
    a.x *= b.x; a.y *= b.y; a.z *= b.z; a.w *= b.w;
}
inline __host__ __device__ float4 operator*(float4 a, float b)
{
    return make_float4(a.x * b, a.y * b, a.z * b,  a.w * b);
}
inline __host__ __device__ float4 operator*(float b, float4 a)
{
    return make_float4(b * a.x, b * a.y, b * a.z, b * a.w);
}
inline __host__ __device__ void operator*=(float4 &a, float b)
{
    a.x *= b; a.y *= b; a.z *= b; a.w *= b;
}

inline __host__ __device__ int4 operator*(int4 a, int4 b)
{
    return make_int4(a.x * b.x, a.y * b.y, a.z * b.z,  a.w * b.w);
}
inline __host__ __device__ void operator*=(int4 &a, int4 b)
{
    a.x *= b.x; a.y *= b.y; a.z *= b.z; a.w *= b.w;
}
inline __host__ __device__ int4 operator*(int4 a, int b)
{
    return make_int4(a.x * b, a.y * b, a.z * b,  a.w * b);
}
inline __host__ __device__ int4 operator*(int b, int4 a)
{
    return make_int4(b * a.x, b * a.y, b * a.z, b * a.w);
}
inline __host__ __device__ void operator*=(int4 &a, int b)
{
    a.x *= b; a.y *= b; a.z *= b; a.w *= b;
}

inline __host__ __device__ uint4 operator*(uint4 a, uint4 b)
{
    return make_uint4(a.x * b.x, a.y * b.y, a.z * b.z,  a.w * b.w);
}
inline __host__ __device__ void operator*=(uint4 &a, uint4 b)
{
    a.x *= b.x; a.y *= b.y; a.z *= b.z; a.w *= b.w;
}
inline __host__ __device__ uint4 operator*(uint4 a, uint b)
{
    return make_uint4(a.x * b, a.y * b, a.z * b,  a.w * b);
}
inline __host__ __device__ uint4 operator*(uint b, uint4 a)
{
    return make_uint4(b * a.x, b * a.y, b * a.z, b * a.w);
}
inline __host__ __device__ void operator*=(uint4 &a, uint b)
{
    a.x *= b; a.y *= b; a.z *= b; a.w *= b;
}

////////////////////////////////////////////////////////////////////////////////
// divide
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float2 operator/(float2 a, float2 b)
{
    return make_float2(a.x / b.x, a.y / b.y);
}
inline __host__ __device__ void operator/=(float2 &a, float2 b)
{
    a.x /= b.x; a.y /= b.y;
}
inline __host__ __device__ float2 operator/(float2 a, float b)
{
    return make_float2(a.x / b, a.y / b);
}
inline __host__ __device__ void operator/=(float2 &a, float b)
{
    a.x /= b; a.y /= b;
}
inline __host__ __device__ float2 operator/(float b, float2 a)
{
    return make_float2(b / a.x, b / a.y);
}

inline __host__ __device__ float3 operator/(float3 a, float3 b)
{
    return make_float3(a.x / b.x, a.y / b.y, a.z / b.z);
}
inline __host__ __device__ void operator/=(float3 &a, float3 b)
{
    a.x /= b.x; a.y /= b.y; a.z /= b.z;
}
inline __host__ __device__ float3 operator/(float3 a, float b)
{
    return make_float3(a.x / b, a.y / b, a.z / b);
}
inline __host__ __device__ void operator/=(float3 &a, float b)
{
    a.x /= b; a.y /= b; a.z /= b;
}
inline __host__ __device__ float3 operator/(float b, float3 a)
{
    return make_float3(b / a.x, b / a.y, b / a.z);
}

inline __host__ __device__ float4 operator/(float4 a, float4 b)
{
    return make_float4(a.x / b.x, a.y / b.y, a.z / b.z,  a.w / b.w);
}
inline __host__ __device__ void operator/=(float4 &a, float4 b)
{
    a.x /= b.x; a.y /= b.y; a.z /= b.z; a.w /= b.w;
}
inline __host__ __device__ float4 operator/(float4 a, float b)
{
    return make_float4(a.x / b, a.y / b, a.z / b,  a.w / b);
}
inline __host__ __device__ void operator/=(float4 &a, float b)
{
    a.x /= b; a.y /= b; a.z /= b; a.w /= b;
}
inline __host__ __device__ float4 operator/(float b, float4 a){
    return make_float4(b / a.x, b / a.y, b / a.z, b / a.w);
}

////////////////////////////////////////////////////////////////////////////////
// min
////////////////////////////////////////////////////////////////////////////////

inline  __host__ __device__ float2 fminf(float2 a, float2 b)
{
	return make_float2(fminf(a.x,b.x), fminf(a.y,b.y));
}
inline __host__ __device__ float3 fminf(float3 a, float3 b)
{
	return make_float3(fminf(a.x,b.x), fminf(a.y,b.y), fminf(a.z,b.z));
}
inline  __host__ __device__ float4 fminf(float4 a, float4 b)
{
	return make_float4(fminf(a.x,b.x), fminf(a.y,b.y), fminf(a.z,b.z), fminf(a.w,b.w));
}

inline __host__ __device__ int2 min(int2 a, int2 b)
{
    return make_int2(min(a.x,b.x), min(a.y,b.y));
}
inline __host__ __device__ int3 min(int3 a, int3 b)
{
    return make_int3(min(a.x,b.x), min(a.y,b.y), min(a.z,b.z));
}
inline __host__ __device__ int4 min(int4 a, int4 b)
{
    return make_int4(min(a.x,b.x), min(a.y,b.y), min(a.z,b.z), min(a.w,b.w));
}

inline __host__ __device__ uint2 min(uint2 a, uint2 b)
{
    return make_uint2(min(a.x,b.x), min(a.y,b.y));
}
inline __host__ __device__ uint3 min(uint3 a, uint3 b)
{
    return make_uint3(min(a.x,b.x), min(a.y,b.y), min(a.z,b.z));
}
inline __host__ __device__ uint4 min(uint4 a, uint4 b)
{
    return make_uint4(min(a.x,b.x), min(a.y,b.y), min(a.z,b.z), min(a.w,b.w));
}

////////////////////////////////////////////////////////////////////////////////
// max
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float2 fmaxf(float2 a, float2 b)
{
	return make_float2(fmaxf(a.x,b.x), fmaxf(a.y,b.y));
}
inline __host__ __device__ float3 fmaxf(float3 a, float3 b)
{
	return make_float3(fmaxf(a.x,b.x), fmaxf(a.y,b.y), fmaxf(a.z,b.z));
}
inline __host__ __device__ float4 fmaxf(float4 a, float4 b)
{
	return make_float4(fmaxf(a.x,b.x), fmaxf(a.y,b.y), fmaxf(a.z,b.z), fmaxf(a.w,b.w));
}

inline __host__ __device__ int2 max(int2 a, int2 b)
{
    return make_int2(max(a.x,b.x), max(a.y,b.y));
}
inline __host__ __device__ int3 max(int3 a, int3 b)
{
    return make_int3(max(a.x,b.x), max(a.y,b.y), max(a.z,b.z));
}
inline __host__ __device__ int4 max(int4 a, int4 b)
{
    return make_int4(max(a.x,b.x), max(a.y,b.y), max(a.z,b.z), max(a.w,b.w));
}

inline __host__ __device__ uint2 max(uint2 a, uint2 b)
{
    return make_uint2(max(a.x,b.x), max(a.y,b.y));
}
inline __host__ __device__ uint3 max(uint3 a, uint3 b)
{
    return make_uint3(max(a.x,b.x), max(a.y,b.y), max(a.z,b.z));
}
inline __host__ __device__ uint4 max(uint4 a, uint4 b)
{
    return make_uint4(max(a.x,b.x), max(a.y,b.y), max(a.z,b.z), max(a.w,b.w));
}

////////////////////////////////////////////////////////////////////////////////
// lerp
// - linear interpolation between a and b, based on value t in [0, 1] range
////////////////////////////////////////////////////////////////////////////////

inline __device__ __host__ float lerp(float a, float b, float t)
{
    return a + t*(b-a);
}
inline __device__ __host__ float2 lerp(float2 a, float2 b, float t)
{
    return a + t*(b-a);
}
inline __device__ __host__ float3 lerp(float3 a, float3 b, float t)
{
    return a + t*(b-a);
}
inline __device__ __host__ float4 lerp(float4 a, float4 b, float t)
{
    return a + t*(b-a);
}

////////////////////////////////////////////////////////////////////////////////
// clamp
// - clamp the value v to be in the range [a, b]
////////////////////////////////////////////////////////////////////////////////

inline __device__ __host__ float clamp(float f, float a, float b)
{
    return fmaxf(a, fminf(f, b));
}
inline __device__ __host__ int clamp(int f, int a, int b)
{
    return max(a, min(f, b));
}
inline __device__ __host__ uint clamp(uint f, uint a, uint b)
{
    return max(a, min(f, b));
}

inline __device__ __host__ float2 clamp(float2 v, float a, float b)
{
    return make_float2(clamp(v.x, a, b), clamp(v.y, a, b));
}
inline __device__ __host__ float2 clamp(float2 v, float2 a, float2 b)
{
    return make_float2(clamp(v.x, a.x, b.x), clamp(v.y, a.y, b.y));
}
inline __device__ __host__ float3 clamp(float3 v, float a, float b)
{
    return make_float3(clamp(v.x, a, b), clamp(v.y, a, b), clamp(v.z, a, b));
}
inline __device__ __host__ float3 clamp(float3 v, float3 a, float3 b)
{
    return make_float3(clamp(v.x, a.x, b.x), clamp(v.y, a.y, b.y), clamp(v.z, a.z, b.z));
}
inline __device__ __host__ float4 clamp(float4 v, float a, float b)
{
    return make_float4(clamp(v.x, a, b), clamp(v.y, a, b), clamp(v.z, a, b), clamp(v.w, a, b));
}
inline __device__ __host__ float4 clamp(float4 v, float4 a, float4 b)
{
    return make_float4(clamp(v.x, a.x, b.x), clamp(v.y, a.y, b.y), clamp(v.z, a.z, b.z), clamp(v.w, a.w, b.w));
}

inline __device__ __host__ int2 clamp(int2 v, int a, int b)
{
    return make_int2(clamp(v.x, a, b), clamp(v.y, a, b));
}
inline __device__ __host__ int2 clamp(int2 v, int2 a, int2 b)
{
    return make_int2(clamp(v.x, a.x, b.x), clamp(v.y, a.y, b.y));
}
inline __device__ __host__ int3 clamp(int3 v, int a, int b)
{
    return make_int3(clamp(v.x, a, b), clamp(v.y, a, b), clamp(v.z, a, b));
}
inline __device__ __host__ int3 clamp(int3 v, int3 a, int3 b)
{
    return make_int3(clamp(v.x, a.x, b.x), clamp(v.y, a.y, b.y), clamp(v.z, a.z, b.z));
}
inline __device__ __host__ int4 clamp(int4 v, int a, int b)
{
    return make_int4(clamp(v.x, a, b), clamp(v.y, a, b), clamp(v.z, a, b), clamp(v.w, a, b));
}
inline __device__ __host__ int4 clamp(int4 v, int4 a, int4 b)
{
    return make_int4(clamp(v.x, a.x, b.x), clamp(v.y, a.y, b.y), clamp(v.z, a.z, b.z), clamp(v.w, a.w, b.w));
}

inline __device__ __host__ uint2 clamp(uint2 v, uint a, uint b)
{
    return make_uint2(clamp(v.x, a, b), clamp(v.y, a, b));
}
inline __device__ __host__ uint2 clamp(uint2 v, uint2 a, uint2 b)
{
    return make_uint2(clamp(v.x, a.x, b.x), clamp(v.y, a.y, b.y));
}
inline __device__ __host__ uint3 clamp(uint3 v, uint a, uint b)
{
    return make_uint3(clamp(v.x, a, b), clamp(v.y, a, b), clamp(v.z, a, b));
}
inline __device__ __host__ uint3 clamp(uint3 v, uint3 a, uint3 b)
{
    return make_uint3(clamp(v.x, a.x, b.x), clamp(v.y, a.y, b.y), clamp(v.z, a.z, b.z));
}
inline __device__ __host__ uint4 clamp(uint4 v, uint a, uint b)
{
    return make_uint4(clamp(v.x, a, b), clamp(v.y, a, b), clamp(v.z, a, b), clamp(v.w, a, b));
}
inline __device__ __host__ uint4 clamp(uint4 v, uint4 a, uint4 b)
{
    return make_uint4(clamp(v.x, a.x, b.x), clamp(v.y, a.y, b.y), clamp(v.z, a.z, b.z), clamp(v.w, a.w, b.w));
}

////////////////////////////////////////////////////////////////////////////////
// dot product
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float dot(float2 a, float2 b)
{ 
    return a.x * b.x + a.y * b.y;
}
inline __host__ __device__ float dot(float3 a, float3 b)
{ 
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline __host__ __device__ float dot(float4 a, float4 b)
{ 
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

inline __host__ __device__ int dot(int2 a, int2 b)
{ 
    return a.x * b.x + a.y * b.y;
}
inline __host__ __device__ int dot(int3 a, int3 b)
{ 
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline __host__ __device__ int dot(int4 a, int4 b)
{ 
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

inline __host__ __device__ uint dot(uint2 a, uint2 b)
{ 
    return a.x * b.x + a.y * b.y;
}
inline __host__ __device__ uint dot(uint3 a, uint3 b)
{ 
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline __host__ __device__ uint dot(uint4 a, uint4 b)
{ 
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

////////////////////////////////////////////////////////////////////////////////
// length
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float length(float2 v)
{
    return sqrtf(dot(v, v));
}
inline __host__ __device__ float length(float3 v)
{
    return sqrtf(dot(v, v));
}
inline __host__ __device__ float length(float4 v)
{
    return sqrtf(dot(v, v));
}

////////////////////////////////////////////////////////////////////////////////
// normalize
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float2 normalize(float2 v)
{
    float invLen = rsqrtf(dot(v, v));
    return v * invLen;
}
inline __host__ __device__ float3 normalize(float3 v)
{
    float invLen = rsqrtf(dot(v, v));
    return v * invLen;
}
inline __host__ __device__ float4 normalize(float4 v)
{
    float invLen = rsqrtf(dot(v, v));
    return v * invLen;
}

////////////////////////////////////////////////////////////////////////////////
// floor
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float2 floorf(float2 v)
{
    return make_float2(floorf(v.x), floorf(v.y));
}
inline __host__ __device__ float3 floorf(float3 v)
{
    return make_float3(floorf(v.x), floorf(v.y), floorf(v.z));
}
inline __host__ __device__ float4 floorf(float4 v)
{
    return make_float4(floorf(v.x), floorf(v.y), floorf(v.z), floorf(v.w));
}

////////////////////////////////////////////////////////////////////////////////
// frac - returns the fractional portion of a scalar or each vector component
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float fracf(float v)
{
    return v - floorf(v);
}
inline __host__ __device__ float2 fracf(float2 v)
{
    return make_float2(fracf(v.x), fracf(v.y));
}
inline __host__ __device__ float3 fracf(float3 v)
{
    return make_float3(fracf(v.x), fracf(v.y), fracf(v.z));
}
inline __host__ __device__ float4 fracf(float4 v)
{
    return make_float4(fracf(v.x), fracf(v.y), fracf(v.z), fracf(v.w));
}

////////////////////////////////////////////////////////////////////////////////
// fmod
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float2 fmodf(float2 a, float2 b)
{
    return make_float2(fmodf(a.x, b.x), fmodf(a.y, b.y));
}
inline __host__ __device__ float3 fmodf(float3 a, float3 b)
{
    return make_float3(fmodf(a.x, b.x), fmodf(a.y, b.y), fmodf(a.z, b.z));
}
inline __host__ __device__ float4 fmodf(float4 a, float4 b)
{
    return make_float4(fmodf(a.x, b.x), fmodf(a.y, b.y), fmodf(a.z, b.z), fmodf(a.w, b.w));
}

////////////////////////////////////////////////////////////////////////////////
// absolute value
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float2 fabs(float2 v)
{
	return make_float2(fabs(v.x), fabs(v.y));
}
inline __host__ __device__ float3 fabs(float3 v)
{
	return make_float3(fabs(v.x), fabs(v.y), fabs(v.z));
}
inline __host__ __device__ float4 fabs(float4 v)
{
	return make_float4(fabs(v.x), fabs(v.y), fabs(v.z), fabs(v.w));
}

inline __host__ __device__ int2 abs(int2 v)
{
	return make_int2(abs(v.x), abs(v.y));
}
inline __host__ __device__ int3 abs(int3 v)
{
	return make_int3(abs(v.x), abs(v.y), abs(v.z));
}
inline __host__ __device__ int4 abs(int4 v)
{
	return make_int4(abs(v.x), abs(v.y), abs(v.z), abs(v.w));
}

////////////////////////////////////////////////////////////////////////////////
// reflect
// - returns reflection of incident ray I around surface normal N
// - N should be normalized, reflected vector's length is equal to length of I
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float3 reflect(float3 i, float3 n)
{
	return i - 2.0f * n * dot(n,i);
}

////////////////////////////////////////////////////////////////////////////////
// cross product
////////////////////////////////////////////////////////////////////////////////

inline __host__ __device__ float3 cross(float3 a, float3 b)
{ 
    return make_float3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x); 
}

////////////////////////////////////////////////////////////////////////////////
// smoothstep
// - returns 0 if x < a
// - returns 1 if x > b
// - otherwise returns smooth interpolation between 0 and 1 based on x
////////////////////////////////////////////////////////////////////////////////

inline __device__ __host__ float smoothstep(float a, float b, float x)
{
	float y = clamp((x - a) / (b - a), 0.0f, 1.0f);
	return (y*y*(3.0f - (2.0f*y)));
}
inline __device__ __host__ float2 smoothstep(float2 a, float2 b, float2 x)
{
	float2 y = clamp((x - a) / (b - a), 0.0f, 1.0f);
	return (y*y*(make_float2(3.0f) - (make_float2(2.0f)*y)));
}
inline __device__ __host__ float3 smoothstep(float3 a, float3 b, float3 x)
{
	float3 y = clamp((x - a) / (b - a), 0.0f, 1.0f);
	return (y*y*(make_float3(3.0f) - (make_float3(2.0f)*y)));
}
inline __device__ __host__ float4 smoothstep(float4 a, float4 b, float4 x)
{
	float4 y = clamp((x - a) / (b - a), 0.0f, 1.0f);
	return (y*y*(make_float4(3.0f) - (make_float4(2.0f)*y)));
}

#endif

/*
 * Copyright 1993-2010 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */
 
#ifndef _CUTIL_INLINE_FUNCTIONS_RUNTIME_H_
#define _CUTIL_INLINE_FUNCTIONS_RUNTIME_H_

#ifdef _WIN32
#ifdef _DEBUG // Do this only in debug mode...
#  define WINDOWS_LEAN_AND_MEAN
#  include <windows.h>
#  include <stdlib.h>
#  undef min
#  undef max
#endif
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <cufft.h>

// We define these calls here, so the user doesn't need to include __FILE__ and __LINE__
// The advantage is the developers gets to use the inline function so they can debug
#define cutilSafeCallNoSync(err)     __cudaSafeCallNoSync(err, __FILE__, __LINE__)
#define cutilSafeCall(err)           __cudaSafeCall      (err, __FILE__, __LINE__)
#if 0
#define cutilSafeThreadSync()        __cudaSafeThreadSync(__FILE__, __LINE__)
#define cufftSafeCall(err)           __cufftSafeCall     (err, __FILE__, __LINE__)
#define cutilCheckError(err)         __cutilCheckError   (err, __FILE__, __LINE__)
#define cutilCheckMsg(msg)           __cutilCheckMsg     (msg, __FILE__, __LINE__)
#define cutilSafeMalloc(mallocCall)  __cutilSafeMalloc   ((mallocCall), __FILE__, __LINE__)
#define cutilCondition(val)          __cutilCondition    (val, __FILE__, __LINE__)
#endif
#define cutilExit(argc, argv)        __cutilExit         (argc, argv)

#if 0
inline void __cutilCondition(int val, char *file, int line) 
{
    if( CUTFalse == cutCheckCondition( val, file, line ) ) {
        exit(EXIT_FAILURE);
    }
}
#endif //0

inline void __cutilExit(int argc, char **argv)
{
    if (!cutCheckCmdLineFlag(argc, (const char**)argv, "noprompt")) {
        printf("\nPress ENTER to exit...\n");
        fflush( stdout);
        fflush( stderr);
        getchar();
    }
    exit(EXIT_SUCCESS);
}

#define MIN(a,b) ((a < b) ? a : b)
#define MAX(a,b) ((a > b) ? a : b)

// Beginning of GPU Architecture definitions
inline int _ConvertSMVer2Cores(int major, int minor)
{
	// Defines for GPU Architecture types (using the SM version to determine the # of cores per SM
	typedef struct {
		int SM; // 0xMm (hexidecimal notation), M = SM Major version, and m = SM minor version
		int Cores;
	} sSMtoCores;

	sSMtoCores nGpuArchCoresPerSM[] = 
	{ { 0x10,  8 },
	  { 0x11,  8 },
	  { 0x12,  8 },
	  { 0x13,  8 },
	  { 0x20, 32 },
	  { 0x21, 48 },
	  {   -1, -1 } 
	};

	int index = 0;
	while (nGpuArchCoresPerSM[index].SM != -1) {
		if (nGpuArchCoresPerSM[index].SM == ((major << 4) + minor) ) {
			return nGpuArchCoresPerSM[index].Cores;
		}
		index++;
	}
	printf("MapSMtoCores undefined SMversion %d.%d!\n", major, minor);
	return -1;
}
// end of GPU Architecture definitions

// This function returns the best GPU (with maximum GFLOPS)
inline int cutGetMaxGflopsDeviceId()
{
	int current_device   = 0, sm_per_multiproc = 0;
	int max_compute_perf = 0, max_perf_device  = 0;
	int device_count     = 0, best_SM_arch     = 0;
	cudaDeviceProp deviceProp;

	cudaGetDeviceCount( &device_count );
	// Find the best major SM Architecture GPU device
	while ( current_device < device_count ) {
		cudaGetDeviceProperties( &deviceProp, current_device );
		if (deviceProp.major > 0 && deviceProp.major < 9999) {
			best_SM_arch = MAX(best_SM_arch, deviceProp.major);
		}
		current_device++;
	}

    // Find the best CUDA capable GPU device
	current_device = 0;
	while( current_device < device_count ) {
		cudaGetDeviceProperties( &deviceProp, current_device );
		if (deviceProp.major == 9999 && deviceProp.minor == 9999) {
		    sm_per_multiproc = 1;
		} else {
			sm_per_multiproc = _ConvertSMVer2Cores(deviceProp.major, deviceProp.minor);
		}

		int compute_perf  = deviceProp.multiProcessorCount * sm_per_multiproc * deviceProp.clockRate;
		if( compute_perf  > max_compute_perf ) {
            // If we find GPU with SM major > 2, search only these
			if ( best_SM_arch > 2 ) {
				// If our device==dest_SM_arch, choose this, or else pass
				if (deviceProp.major == best_SM_arch) {	
					max_compute_perf  = compute_perf;
					max_perf_device   = current_device;
				}
			} else {
				max_compute_perf  = compute_perf;
				max_perf_device   = current_device;
			}
		}
		++current_device;
	}
	return max_perf_device;
}

#if 0
// This function returns the best GPU (with maximum GFLOPS)
inline int cutGetMaxGflopsGraphicsDeviceId()
{
	int current_device   = 0, sm_per_multiproc = 0;
	int max_compute_perf = 0, max_perf_device  = 0;
	int device_count     = 0, best_SM_arch     = 0;
	int bTCC = 0;
	cudaDeviceProp deviceProp;

	cudaGetDeviceCount( &device_count );
	// Find the best major SM Architecture GPU device that is graphics capable
	while ( current_device < device_count ) {
		cudaGetDeviceProperties( &deviceProp, current_device );

#if CUDA_VERSION >= 3020
		if (deviceProp.tccDriver) bTCC = 1;
#else
		// Assume a Tesla GPU is running in TCC if we are running CUDA 3.1
		if (deviceProp.name[0] == 'T') bTCC = 1;
#endif

		if (!bTCC) {
			if (deviceProp.major > 0 && deviceProp.major < 9999) {
				best_SM_arch = MAX(best_SM_arch, deviceProp.major);
			}
		}
		current_device++;
	}

    // Find the best CUDA capable GPU device
	current_device = 0;
	while( current_device < device_count ) {
		cudaGetDeviceProperties( &deviceProp, current_device );
		if (deviceProp.major == 9999 && deviceProp.minor == 9999) {
		    sm_per_multiproc = 1;
		} else {
			sm_per_multiproc = _ConvertSMVer2Cores(deviceProp.major, deviceProp.minor);
		}

#if CUDA_VERSION >= 3020
		if (deviceProp.tccDriver) bTCC = 1;
#else
		// Assume a Tesla GPU is running in TCC if we are running CUDA 3.1
		if (deviceProp.name[0] == 'T') bTCC = 1;
#endif

		if (!bTCC) // Is this GPU running the TCC driver?  If so we pass on this
		{
			int compute_perf  = deviceProp.multiProcessorCount * sm_per_multiproc * deviceProp.clockRate;
			if( compute_perf  > max_compute_perf ) {
				// If we find GPU with SM major > 2, search only these
				if ( best_SM_arch > 2 ) {
					// If our device==dest_SM_arch, choose this, or else pass
					if (deviceProp.major == best_SM_arch) {	
						max_compute_perf  = compute_perf;
						max_perf_device   = current_device;
					}
				} else {
					max_compute_perf  = compute_perf;
					max_perf_device   = current_device;
				}
			}
		}
		++current_device;
	}
	return max_perf_device;
}
#endif //0

// Give a little more for Windows : the console window often disapears before we can read the message
#ifdef _WIN32
# if 1//ndef UNICODE
#  ifdef _DEBUG // Do this only in debug mode...
	inline void VSPrintf(FILE *file, LPCSTR fmt, ...)
	{
		size_t fmt2_sz	= 2048;
		char *fmt2		= (char*)malloc(fmt2_sz);
		va_list  vlist;
		va_start(vlist, fmt);
		while((_vsnprintf(fmt2, fmt2_sz, fmt, vlist)) < 0) // means there wasn't anough room
		{
			fmt2_sz *= 2;
			if(fmt2) free(fmt2);
			fmt2 = (char*)malloc(fmt2_sz);
		}
		OutputDebugStringA(fmt2);
		fprintf(file, fmt2);
		free(fmt2);
	}
#	define FPRINTF(a) VSPrintf a
#  else //debug
#	define FPRINTF(a) fprintf a
// For other than Win32
#  endif //debug
# else //unicode
// Unicode case... let's give-up for now and keep basic printf
#	define FPRINTF(a) fprintf a
# endif //unicode
#else //win32
#	define FPRINTF(a) fprintf a
#endif //win32

// NOTE: "%s(%i) : " allows Visual Studio to directly jump to the file at the right line
// when the user double clicks on the error line in the Output pane. Like any compile error.

inline void __cudaSafeCallNoSync( cudaError err, const char *file, const int line )
{
    if( cudaSuccess != err) {
        FPRINTF((stderr, "%s(%i) : cudaSafeCallNoSync() Runtime API error : %s.\n",
                file, line, cudaGetErrorString( err) ));
        exit(-1);
    }
}

inline void __cudaSafeCall( cudaError err, const char *file, const int line )
{
    if( cudaSuccess != err) {
		FPRINTF((stderr, "%s(%i) : cudaSafeCall() Runtime API error : %s.\n",
                file, line, cudaGetErrorString( err) ));
        exit(-1);
    }
}

#if 0
inline void __cudaSafeThreadSync( const char *file, const int line )
{
    cudaError err = cudaThreadSynchronize();
    if ( cudaSuccess != err) {
        FPRINTF((stderr, "%s(%i) : cudaThreadSynchronize() Driver API error : %s.\n",
                file, line, cudaGetErrorString( err) ));
        exit(-1);
    }
}

inline void __cufftSafeCall( cufftResult err, const char *file, const int line )
{
    if( CUFFT_SUCCESS != err) {
        FPRINTF((stderr, "%s(%i) : cufftSafeCall() CUFFT error.\n",
                file, line));
        exit(-1);
    }
}

inline void __cutilCheckError( CUTBoolean err, const char *file, const int line )
{
    if( CUTTrue != err) {
        FPRINTF((stderr, "%s(%i) : CUTIL CUDA error.\n",
                file, line));
        exit(-1);
    }
}

inline void __cutilCheckMsg( const char *errorMessage, const char *file, const int line )
{
    cudaError_t err = cudaGetLastError();
    if( cudaSuccess != err) {
        FPRINTF((stderr, "%s(%i) : cutilCheckMsg() CUTIL CUDA error : %s : %s.\n",
                file, line, errorMessage, cudaGetErrorString( err) ));
        exit(-1);
    }
#ifdef _DEBUG
    err = cudaThreadSynchronize();
    if( cudaSuccess != err) {
		FPRINTF((stderr, "%s(%i) : cutilCheckMsg cudaThreadSynchronize error: %s : %s.\n",
                file, line, errorMessage, cudaGetErrorString( err) ));
        exit(-1);
    }
#endif
}
inline void __cutilSafeMalloc( void *pointer, const char *file, const int line )
{
    if( !(pointer)) {
        FPRINTF((stderr, "%s(%i) : cutilSafeMalloc host malloc failure\n",
                file, line));
        exit(-1);
    }
}
#endif //0
#if __DEVICE_EMULATION__
    inline int cutilDeviceInit(int ARGC, char **ARGV) { }
    inline int cutilChooseCudaDevice(int ARGC, char **ARGV) { }
#else
    inline int cutilDeviceInit(int ARGC, char **ARGV)
    {
        int deviceCount;
        cutilSafeCallNoSync(cudaGetDeviceCount(&deviceCount));
        if (deviceCount == 0) {
            FPRINTF((stderr, "CUTIL CUDA error: no devices supporting CUDA.\n"));
            exit(-1);
        }
        int dev = 0;
        cutGetCmdLineArgumenti(ARGC, (const char **) ARGV, "device", &dev);
        if (dev < 0) 
            dev = 0;
        if (dev > deviceCount-1) {
            fprintf(stderr, "cutilDeviceInit (Device=%d) invalid GPU device.  %d GPU device(s) detected.\n\n", dev, deviceCount);
            return -dev;
        }  
        cudaDeviceProp deviceProp;
        cutilSafeCallNoSync(cudaGetDeviceProperties(&deviceProp, dev));
        if (deviceProp.major < 1) {
            FPRINTF((stderr, "cutil error: GPU device does not support CUDA.\n"));
            exit(-1);                                                  \
        }
        printf("> Using CUDA device [%d]: %s\n", dev, deviceProp.name);
        cutilSafeCall(cudaSetDevice(dev));

        return dev;
    }

    // General initialization call to pick the best CUDA Device
    inline int cutilChooseCudaDevice(int argc, char **argv)
    {
        cudaDeviceProp deviceProp;
        int devID = 0;
        // If the command-line has a device number specified, use it
        if( cutCheckCmdLineFlag(argc, (const char**)argv, "device") ) {
            devID = cutilDeviceInit(argc, argv);
            if (devID < 0) {
               printf("exiting...\n");
               cutilExit(argc, argv);
               exit(0);
            }
        } else {
            // Otherwise pick the device with highest Gflops/s
            devID = cutGetMaxGflopsDeviceId();
            cutilSafeCallNoSync( cudaSetDevice( devID ) );
            cutilSafeCallNoSync( cudaGetDeviceProperties(&deviceProp, devID) );
            printf("> Using CUDA device [%d]: %s\n", devID, deviceProp.name);
        }
        return devID;
    }
#endif

#if 0
//! Check for CUDA context lost
inline void cutilCudaCheckCtxLost(const char *errorMessage, const char *file, const int line ) 
{
    cudaError_t err = cudaGetLastError();
    if( cudaSuccess != err) {
        FPRINTF((stderr, "%s(%i) : CUDA error: %s : %s.\n",
        file, line, errorMessage, cudaGetErrorString( err) ));
        exit(-1);
    }
    err = cudaThreadSynchronize();
    if( cudaSuccess != err) {
        FPRINTF((stderr, "%s(%i) : CCUDA error: %s : %s.\n",
        file, line, errorMessage, cudaGetErrorString( err) ));
        exit(-1);
    }
}

// General check for CUDA GPU SM Capabilities
inline bool cutilCudaCapabilities(int major_version, int minor_version)
{
    cudaDeviceProp deviceProp;
    deviceProp.major = 0;
    deviceProp.minor = 0;
    int dev;

#ifdef __DEVICE_EMULATION__
    printf("> Compute Device Emulation Mode \n");
#endif

    cutilSafeCall( cudaGetDevice(&dev) );
    cutilSafeCall( cudaGetDeviceProperties(&deviceProp, dev));

    if((deviceProp.major > major_version) ||
	   (deviceProp.major == major_version && deviceProp.minor >= minor_version))
    {
        printf("> Device %d: <%16s >, Compute SM %d.%d detected\n", dev, deviceProp.name, deviceProp.major, deviceProp.minor);
        return true;
    }
    else
    {
        printf("There is no device supporting CUDA compute capability %d.%d.\n", major_version, minor_version);
        printf("PASSED\n");
        return false;
    }
}
#endif //0

#endif // _CUTIL_INLINE_FUNCTIONS_RUNTIME_H_
