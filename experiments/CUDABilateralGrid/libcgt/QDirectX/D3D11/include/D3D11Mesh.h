#ifndef D3D11_MESH_H
#define D3D11_MESH_H

#include <memory>
#include <QHash>
#include <QString>

#include <geometry/BoundingBox3f.h>
#include <vecmath/Matrix4f.h>
#include <vecmath/Vector2i.h>

#include <DynamicVertexBuffer.h>
#include <DynamicTexture2D.h>
#include <VertexPosition4fNormal3fColor4fTexture2f.h>

class OBJData;

// TODO: just templatize this thing
class D3D11Mesh
{
public:

	// capacity is in number of vertices
	D3D11Mesh( ID3D11Device* pDevice, int capacity );
	D3D11Mesh( ID3D11Device* pDevice, VertexPosition4fNormal3fColor4fTexture2f* vertexArray, int capacity );
	
	// TODO: make this not a constructor
	// TODO: texture path is pretty silly
	D3D11Mesh( ID3D11Device* pDevice, std::shared_ptr< OBJData > pOBJData, QString texturePath, bool generatePerFaceNormalsIfNonExistent = true );

	virtual ~D3D11Mesh();

	int capacity() const;
	
	std::vector< Vector2i >& vertexRanges();
	std::vector< Vector3f >& diffuseColors();
	std::vector< Vector4f >& specularColors();
	std::vector< std::shared_ptr< DynamicTexture2D > >& diffuseTextures()
	{
		return m_diffuseTexturesRanges;
	}

	//void addVertexRange( const Vector2i& vr, const Vector3f& kd );
	void addVertexRange( const Vector2i& vr,
		const Vector3f& kd, const Vector4f& ks = Vector4f( 0, 0, 0, 0 ),
		std::shared_ptr< DynamicTexture2D > pDiffuseTexture = nullptr );

	std::vector< VertexPosition4fNormal3fColor4fTexture2f >& vertexArray();
	const std::vector< VertexPosition4fNormal3fColor4fTexture2f >& vertexArray() const;

	// updates the backing vertex buffer on the GPU
	// by copying from the cpu vertex array
	void updateGPUBuffer();

	// object space bounding box
	const BoundingBox3f& boundingBox() const;
	
	// returns the matrix that centers this object at the origin
	// and uniformly scales the object such that axis (0, 1, or 2 for x, y, z, or -1 for longest)
	// of its bounding box maps to [-1,1]
	Matrix4f twoUnitCubeWorldMatrix( int axis = -1 ) const;

	Matrix4f worldMatrix() const;
	void setWorldMatrix( const Matrix4f& m );

	// get the inverse transpose of the top-left 3x3 submatrix of worldMatrix()
	Matrix3f normalMatrix() const;
	// for Direct3D: returns normalMatrix() as a 4x4 matrix (with 0 everywhere else)
	Matrix4f normalMatrix4x4() const;

	std::shared_ptr< DynamicVertexBuffer > vertexBuffer() const;

private:
	
	void updateBoundingBox();

	BoundingBox3f m_boundingBox;

	Matrix4f m_worldMatrix;

	ID3D11Device* m_pDevice;
	std::vector< VertexPosition4fNormal3fColor4fTexture2f > m_vertexArray;
	std::shared_ptr< DynamicVertexBuffer > m_pVertexBuffer;

	std::vector< Vector2i > m_vertexRanges;
	std::vector< Vector3f > m_diffuseColors;
	std::vector< Vector4f > m_specularColors;
	std::vector< std::shared_ptr< DynamicTexture2D > > m_diffuseTexturesRanges;

	QHash< QString, std::shared_ptr< DynamicTexture2D > > m_diffuseTextures;

};

#endif // D3D11_MESH_H
