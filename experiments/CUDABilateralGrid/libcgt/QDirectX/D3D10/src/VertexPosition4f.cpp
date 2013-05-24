#include "VertexPosition4f.h"

VertexPosition4f::VertexPosition4f()
{

}

VertexPosition4f::VertexPosition4f( float x, float y, float z, float w ) :
	m_position( x, y, z, w )
{

}

VertexPosition4f::VertexPosition4f( Vector4f position ) :
	m_position( position )
{

}

// static
int VertexPosition4f::numElements()
{
	return 1;
}

// static
int VertexPosition4f::sizeInBytes()
{
	return 4 * sizeof( float );
}

// static
D3D10_INPUT_ELEMENT_DESC VertexPosition4f::s_layout[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 }
};
