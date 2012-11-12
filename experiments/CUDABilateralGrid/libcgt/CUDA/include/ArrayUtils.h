#ifndef ARRAY_UTILS_H
#define ARRAY_UTILS_H

#include <common/Array2D.h>
#include <vector_types.h>

class ArrayUtils
{
public:

	static bool saveTXT( const Array2D< float2 >& array, const char* filename );
	static bool saveTXT( const Array2D< float4 >& array, const char* filename );
};

#endif ARRAY_UTILS_H
