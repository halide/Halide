#include "D3D11Utils_Box.h"

// static
D3D11_BOX D3D11Utils_Box::createRange( uint x, uint width )
{
	return createRect( x, 0, width, 1 );
}

// static
D3D11_BOX D3D11Utils_Box::createRect( uint x, uint y, uint width, uint height )
{
	return createBox( x, y, 0, width, height, 1 );
}

// static
D3D11_BOX D3D11Utils_Box::createBox( uint x, uint y, uint z, uint width, uint height, uint depth )
{
	// D3D11_BOX is a half-open interval
	D3D11_BOX box;

	box.left = x;
	box.right = x + width;
	box.top = y;
	box.bottom = y + height;
	box.front = z;
	box.back = z + depth;

	return box;
}