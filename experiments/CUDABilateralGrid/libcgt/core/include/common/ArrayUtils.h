#ifndef ARRAY_UTIL_H
#define ARRAY_UTIL_H

#include "ArrayWithLength.h"
#include "Array2D.h"
#include "Array3D.h"

class ArrayUtils
{
public:

	static ArrayWithLength< float > createFloatArray( int length, float fillValue );
	static bool saveTXT( const ArrayWithLength< float >& array, const char* filename );
	static bool saveTXT( const Array2D< float >& array, const char* filename );
	static bool saveTXT( const Array3D< float >& array, const char* filename );
};

#endif // ARRAY_UTIL_H
