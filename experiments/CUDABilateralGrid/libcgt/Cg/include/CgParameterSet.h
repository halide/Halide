#ifndef CG_PARAMETER_SET_H
#define CG_PARAMETER_SET_H

#include <GL/glew.h>
#include <Cg/cg.h>
#include <Cg/cgGL.h>
#include <GL/GLTexture.h>
#include <vecmath/Matrix4f.h>
#include <vecmath/Vector2f.h>
#include <vecmath/Vector3f.h>
#include <vecmath/Vector4f.h>

#include <QHash>
#include <QString>

class CgParameterSet
{
public:

	enum CgStateMatrix
	{
		CgStateMatrix_MODELVIEW = CG_GL_MODELVIEW_MATRIX,
		CgStateMatrix_PROJECTION = CG_GL_PROJECTION_MATRIX,
		CgStateMatrix_TEXTURE = CG_GL_TEXTURE_MATRIX,
		CgStateMatrix_MODELVIEW_PROJECTION = CG_GL_MODELVIEW_PROJECTION_MATRIX
	};

	enum CgStateMatrixTransform
	{
		CgStateMatrixTransform_IDENTITY = CG_GL_MATRIX_IDENTITY,
		CgStateMatrixTransform_TRANSPOSE = CG_GL_MATRIX_TRANSPOSE,
		CgStateMatrixTransform_INVERSE = CG_GL_MATRIX_INVERSE,
		CgStateMatrixTransform_INVERSE_TRANSPOSE = CG_GL_MATRIX_INVERSE_TRANSPOSE
	};

	CgParameterSet( QHash< QString, CGparameter > parameterNames );

	void setStateMatrixParameter( QString name, CgStateMatrix matrix = CgStateMatrix_MODELVIEW_PROJECTION, CgStateMatrixTransform transform = CgStateMatrixTransform_IDENTITY );

	void setFloatParameter( QString name, float* f );
	void setFloat2Parameter( QString name, Vector2f* v );
	void setFloat3Parameter( QString name, Vector3f* v );
	void setFloat4Parameter( QString name, Vector4f* v );

	void setFloat4x4Parameter( QString name, Matrix4f* v );

	void setTextureParameter( QString name, GLTexture* t );

	void applyAll();

private:

	QHash< QString, CGparameter > m_parameterNames;

	QHash< QString, QPair< CgStateMatrix, CgStateMatrixTransform > > m_stateMatrixParameterTable;

	QHash< QString, float* > m_floatParameterTable;
	QHash< QString, Vector2f* > m_float2ParameterTable;
	QHash< QString, Vector3f* > m_float3ParameterTable;
	QHash< QString, Vector4f* > m_float4ParameterTable;
	
	QHash< QString, Matrix4f* > m_float4x4ParameterTable;
	
	QHash< QString, GLTexture* > m_textureParameterTable;

};

#endif // CG_PARAMETER_SET_H
