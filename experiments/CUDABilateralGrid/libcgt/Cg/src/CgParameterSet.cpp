#if 0
// TODO: refactor into its own project

#include "Cg/CgParameterSet.h"

#include <cassert>
#include <QDebug>

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

CgParameterSet::CgParameterSet( QHash< QString, CGparameter > parameterNames ) :

m_parameterNames( parameterNames )

{

}

void CgParameterSet::setStateMatrixParameter( QString name, CgStateMatrix matrix, CgStateMatrixTransform transform )
{
	assert( m_parameterNames.contains( name ) );
	m_stateMatrixParameterTable.insert( name, qMakePair( matrix, transform ) );
}

void CgParameterSet::setFloatParameter( QString name, float* f )
{
	assert( m_parameterNames.contains( name ) );
	m_floatParameterTable.insert( name, f );
}

void CgParameterSet::setFloat2Parameter( QString name, Vector2f* v )
{
	assert( m_parameterNames.contains( name ) );
	m_float2ParameterTable.insert( name, v );
}

void CgParameterSet::setFloat3Parameter( QString name, Vector3f* v )
{
	assert( m_parameterNames.contains( name ) );
	m_float3ParameterTable.insert( name, v );
}

void CgParameterSet::setFloat4Parameter( QString name, Vector4f* v )
{
	assert( m_parameterNames.contains( name ) );
	m_float4ParameterTable.insert( name, v );
}

void CgParameterSet::setFloat4x4Parameter( QString name, Matrix4f* m )
{
	assert( m_parameterNames.contains( name ) );
	m_float4x4ParameterTable.insert( name, m );
}

void CgParameterSet::setTextureParameter( QString name, GLTexture* t )
{
	assert( m_parameterNames.contains( name ) );
	m_textureParameterTable.insert( name, t );
}

void CgParameterSet::applyAll()
{
	int nParametersInProgram = m_parameterNames.size();
	int nParametersSet =
		m_stateMatrixParameterTable.size() + 
		m_floatParameterTable.size() +
		m_float2ParameterTable.size() +
		m_float3ParameterTable.size() +
		m_float4ParameterTable.size() +

		m_float4x4ParameterTable.size() +

		m_textureParameterTable.size();

	assert( nParametersInProgram == nParametersSet );

	// state matrices
	foreach( QString p, m_stateMatrixParameterTable.keys() )
	{
		QPair< CgStateMatrix, CgStateMatrixTransform > mt = m_stateMatrixParameterTable.value( p );
		cgGLSetStateMatrixParameter( m_parameterNames.value( p ),
			static_cast< CGGLenum >( mt.first ),
			static_cast< CGGLenum >( mt.second ) );
	}

	// float1
	foreach( QString p, m_floatParameterTable.keys() )
	{
		float* f = m_floatParameterTable.value( p );
		cgGLSetParameter1f( m_parameterNames.value( p ), *f );
	}

	// float2
	foreach( QString p, m_float2ParameterTable.keys() )
	{
		Vector2f* v = m_float2ParameterTable.value( p );
		cgGLSetParameter2f( m_parameterNames.value( p ), v->x(), v->y() );
	}

	// float3
	foreach( QString p, m_float3ParameterTable.keys() )
	{
		Vector3f* v = m_float3ParameterTable.value( p );
		cgGLSetParameter3f( m_parameterNames.value( p ), v->x(), v->y(), v->z() );
	}

	// float4
	foreach( QString p, m_float4ParameterTable.keys() )
	{
		Vector4f* v = m_float4ParameterTable.value( p );
		cgGLSetParameter4f( m_parameterNames.value( p ), v->x(), v->y(), v->z(), v->w() );
	}

	// float4x4
	foreach( QString p, m_float4x4ParameterTable.keys() )
	{
		Matrix4f* m = m_float4x4ParameterTable.value( p );
		cgGLSetMatrixParameterfc( m_parameterNames.value( p ), *m );
	}

	// texture
	foreach( QString p, m_textureParameterTable.keys() )
	{
		GLTexture* t = m_textureParameterTable.value( p );
		cgGLSetTextureParameter( m_parameterNames.value( p ), t->getTextureId() );		
	}
}

#endif