#pragma once

#include <utility>

#include "vecmath/Vector2i.h"

class Comparators
{
public:
	
	// returns true if x.second < y.second
	// Useful for sorting array indices based on distance.
	static bool indexAndDistanceLess( const std::pair< int, float >& a, const std::pair< int, float >& b );	
};

namespace std
{
	template<>
	struct less< Vector2i >
	{
		bool operator () ( const Vector2i& a, const Vector2i& b );
	};
}
