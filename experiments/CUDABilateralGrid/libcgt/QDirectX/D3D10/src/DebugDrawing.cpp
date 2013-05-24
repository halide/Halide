#include "DebugDrawing.h"
#include "D3D10Utils.h"
#include "VertexPosition4fColor4f.h"
#include <vector>
// --------------------------------------------------------------------------

DebugDrawing* DebugDrawing::s_pInstance = 0;
// --------------------------------------------------------------------------

#pragma pack(push,1)
struct DebugVertex
{
	Vector4f	P;	// position
	Vector4f	C;	// color
};
#pragma pack(pop)
// --------------------------------------------------------------------------

static std::vector< DebugVertex > s_pointStream;
static std::vector< DebugVertex > s_lineStream;
static std::vector< DebugVertex > s_triangleStream;
// --------------------------------------------------------------------------

#define DEBUG_VB_SIZE 16384
// --------------------------------------------------------------------------

void DebugDrawing::init( ID3D10Device* pDevice, Reference< EffectManager > mgr )
{
	s_pInstance = new DebugDrawing( pDevice, mgr );
}
// --------------------------------------------------------------------------

DebugDrawing::DebugDrawing( ID3D10Device* pDevice, Reference< EffectManager > mgr )
{
	m_pDevice = pDevice;
	m_vb = new DynamicVertexBuffer( m_pDevice, DEBUG_VB_SIZE, sizeof(DebugVertex) );

	bool bCompile = mgr->loadFromFile( "debugline", "shaders\\DebugDrawing.fx" );
	if ( !bCompile )
		exit( 0 );
	m_pEffect = mgr->getEffect( "debugline" );

	ID3D10EffectTechnique* pTech = m_pEffect->GetTechniqueByIndex( 0 );
	ID3D10EffectPass* pPass = pTech->GetPassByIndex( 0 );

	m_pInputLayout = D3D10Utils::createInputLayout< VertexPosition4fColor4f >( pDevice, pPass );
}
// --------------------------------------------------------------------------

DebugDrawing::~DebugDrawing()
{
}
// --------------------------------------------------------------------------

DebugDrawing* DebugDrawing::instance()
{
	return s_pInstance;
}
// --------------------------------------------------------------------------

void DebugDrawing::reset()
{
	s_pointStream.resize( 0 );
	s_lineStream.resize( 0 );
	s_triangleStream.resize( 0 );
}
// --------------------------------------------------------------------------

void DebugDrawing::addPoint( const Vector3f& v, const Vector3f& c )
{
	DebugVertex P0;
	P0.P = Vector4f( v, 1.0f );
	P0.C = Vector4f( c, 1.0f );
	s_pointStream.push_back( P0 );
}
// --------------------------------------------------------------------------

void DebugDrawing::addLine( const Vector3f& v0, const Vector3f& v1 )
{
	Vector3f white( 1.0f, 1.0f, 1.0f );
	addLine( v0, v1, white, white );
}
// --------------------------------------------------------------------------

void DebugDrawing::addLine( const Vector3f& v0, const Vector3f& v1, const Vector3f& c0, const Vector3f& c1 )
{
	DebugVertex P0, P1;
	P0.P = Vector4f( v0, 1.0f );
	P0.C = Vector4f( c0, 1.0f );
	P1.P = Vector4f( v1, 1.0f );
	P1.C = Vector4f( c1, 1.0f );
	s_lineStream.push_back( P0 );
	s_lineStream.push_back( P1 );
}
// --------------------------------------------------------------------------


void DebugDrawing::addBox( const Vector3f& vmin, const Vector3f& vmax )
{
	Vector3f corners[8];
	for ( int i = 0; i < 8; ++i )
		corners[ i ] = Vector3f( (i & 0x01) ? vmax.x() : vmin.x(), 
								 (i & 0x02) ? vmax.y() : vmin.y(),
								 (i & 0x04) ? vmax.z() : vmin.z() );

	addLine( corners[ 0 ], corners[ 1 ] );
	addLine( corners[ 2 ], corners[ 3 ] );
	addLine( corners[ 0 ], corners[ 2 ] );
	addLine( corners[ 1 ], corners[ 3 ] );
	addLine( corners[ 4 ], corners[ 5 ] );
	addLine( corners[ 6 ], corners[ 7 ] );
	addLine( corners[ 4 ], corners[ 6 ] );
	addLine( corners[ 5 ], corners[ 7 ] );
	addLine( corners[ 0 ], corners[ 4 ] );
	addLine( corners[ 1 ], corners[ 5 ] );
	addLine( corners[ 2 ], corners[ 6 ] );
	addLine( corners[ 3 ], corners[ 7 ] );
}
// --------------------------------------------------------------------------

void DebugDrawing::addTriangle( const Vector3f& v0, const Vector3f& v1, const Vector3f& v2, const Vector3f& c0, const Vector3f& c1, const Vector3f& c2 )
{
	DebugVertex P;
	P.P = Vector4f( v0, 1.0f );
	P.C = Vector4f( c0, 1.0f );
	s_triangleStream.push_back( P );
	P.P = Vector4f( v1, 1.0f );
	P.C = Vector4f( c1, 1.0f );
	s_triangleStream.push_back( P );
	P.P = Vector4f( v2, 1.0f );
	P.C = Vector4f( c2, 1.0f );
	s_triangleStream.push_back( P );
}

void DebugDrawing::draw( const Matrix4f& mWorldToClip )
{
	m_pEffect->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0 );

	UINT stride = m_vb->defaultStride();
	UINT offset = 0;
	ID3D10Buffer* pBuffer = m_vb->buffer();
	m_pDevice->IASetVertexBuffers( 0, 1, &pBuffer, &stride, &offset );
	m_pDevice->IASetInputLayout( m_pInputLayout );

	for( int primitivetype = 0; primitivetype < 3; ++primitivetype )
	{
		std::vector< DebugVertex >* pVec = 0;
		int iPrimitiveSize = 0;
		switch( primitivetype )
		{
		case 0:
			m_pDevice->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_POINTLIST );
			pVec = &s_pointStream;
			iPrimitiveSize = 1;
			break;
		case 1:
			m_pDevice->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_LINELIST );
			pVec = &s_lineStream;
			iPrimitiveSize = 2;
			break;
		case 2:
			m_pDevice->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
			pVec = &s_triangleStream;
			iPrimitiveSize = 3;
			break;
		default:
			printf( "Invalid primitive type!" );
			exit( 0 );
		}

		size_t start = 0;

		m_pEffect->GetVariableByName( "mWorldToClip" )->AsMatrix()->SetMatrix( mWorldToClip.transposed() );

		while( start < pVec->size() )
		{
			void* pMem = m_vb->mapForWriteDiscard();
			// quantize iCount to primitivesize
			size_t iCount = min( DEBUG_VB_SIZE, pVec->size()-start ) / iPrimitiveSize;
			iCount *= iPrimitiveSize;
			memcpy( pMem, &(*pVec)[ start ], iCount*sizeof(DebugVertex) );
			m_vb->unmap();
			m_pDevice->Draw( (UINT)iCount, 0 );
			start += iCount;
		}
	}
}
// --------------------------------------------------------------------------
