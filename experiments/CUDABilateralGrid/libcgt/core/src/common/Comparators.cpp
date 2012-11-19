#include "common/Comparators.h"

// static
bool Comparators::indexAndDistanceLess( const std::pair< int, float >& a, const std::pair< int, float >& b )
{
	return a.second < b.second;
}

bool std::less< Vector2i >::operator () ( const Vector2i& a, const Vector2i& b )
{
	if( a.x < b.x )
	{
		return true;
	}
	else if( a.x > b.x )
	{
		return false;
	}
	else
	{
		return a.y < b.y;
	}
}
