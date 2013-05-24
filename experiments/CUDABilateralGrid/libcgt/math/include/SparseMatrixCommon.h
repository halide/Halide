#pragma once

#include <map>
#include <unordered_map>
#include <utility>

#include <common/BasicTypes.h>

enum MatrixTriangle
{
	LOWER,
	UPPER
};

enum MatrixType
{
	GENERAL,
	SYMMETRIC,
	TRIANGULAR
};

typedef std::pair< uint, uint > SparseMatrixKey;

// compare j first, then i
struct SparseMatrixKeyColMajorLess
{
	bool operator () ( const SparseMatrixKey& a, const SparseMatrixKey& b ) const;
};

struct SparseMatrixKeyHash
{
	size_t operator () ( const SparseMatrixKey& x ) const;
};


typedef std::map< SparseMatrixKey, uint > SparseMatrixStructureTreeMap;
typedef std::unordered_map< SparseMatrixKey, uint, SparseMatrixKeyHash > SparseMatrixStructureHashMap;