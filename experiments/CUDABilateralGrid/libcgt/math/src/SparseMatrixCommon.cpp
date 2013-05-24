#include "SparseMatrixCommon.h"

bool SparseMatrixKeyColMajorLess::operator () ( const SparseMatrixKey& a, const SparseMatrixKey& b ) const
{
	if( a.second < b.second )
	{
		return true;
	}
	else if( a.second > b.second )
	{
		return false;
	}
	else
	{
		return( a.first < b.first );
	}
}

size_t SparseMatrixKeyHash::operator() ( const SparseMatrixKey& x ) const
{
	return std::hash< uint >()( x.first ) ^ std::hash< uint >()( x.second );
}
