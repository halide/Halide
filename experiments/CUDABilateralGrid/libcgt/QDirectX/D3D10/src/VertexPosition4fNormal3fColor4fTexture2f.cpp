#include "VertexPosition4fNormal3fColor4fTexture2f.h"

VertexPosition4fNormal3fColor4fTexture2f::VertexPosition4fNormal3fColor4fTexture2f()
{

}

VertexPosition4fNormal3fColor4fTexture2f::VertexPosition4fNormal3fColor4fTexture2f( Vector4f position, Vector3f normal, Vector4f color, Vector2f texture ) :
	m_position( position ),
	m_normal( normal ),
	m_color( color ),
	m_texture( texture )
{

}    

// static
int VertexPosition4fNormal3fColor4fTexture2f::numElements()
{
	return 4;
}

// static
int VertexPosition4fNormal3fColor4fTexture2f::sizeInBytes()
{
	return 13 * sizeof( float );
}

// static
D3D10_INPUT_ELEMENT_DESC VertexPosition4fNormal3fColor4fTexture2f::s_layout[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
	{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 4 * sizeof( float ), D3D10_INPUT_PER_VERTEX_DATA, 0 },
	{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 7 * sizeof( float ), D3D10_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 11 * sizeof( float ), D3D10_INPUT_PER_VERTEX_DATA, 0 }
};
