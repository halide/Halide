#ifndef OBJ_LOADER_H
#define OBJ_LOADER_H

#include <memory>
#include <QString>

class OBJData;
class OBJGroup;

class OBJLoader
{
public:

	static std::shared_ptr< OBJData > loadFile( QString objFilename );

private:

	static bool parseOBJ( QString objFilename, std::shared_ptr< OBJData > pOBJData );
	static bool parseMTL( QString mtlFilename, std::shared_ptr< OBJData > pOBJData );

	// parses a position line (starting with "v")
	// returns false on an error
	static bool parsePosition( int lineNumber, QString line,
		QStringList tokens, std::shared_ptr< OBJData > pOBJData );

	// parses a texture coordinate line (starting with "vt")
	// returns false on an error
	static bool parseTextureCoordinate( int lineNumber, QString line,
		QStringList tokens, std::shared_ptr< OBJData > pOBJData );

	// parses a normal line (starting with "vn")
	// returns false on an error
	static bool parseNormal( int lineNumber, QString line,
		QStringList tokens, std::shared_ptr< OBJData > pOBJData );

	// parses a face line (starting with "f" or "fo")
	// returns false on an error
	static bool parseFace( int lineNumber, QString line,
		QStringList tokens, OBJGroup* pCurrentGroup );

	// given the tokens in a face line
	// returns if the attached attributes are consistent
	static bool isFaceLineAttributesConsistent( QStringList tokens,
		bool* pHasTextureCoordinates, bool* pHasNormals );

	// objFaceVertexToken is something of the form:
	// "int"
	// "int/int"
	// "int/int/int", 
	// "int//int"
	// i.e. one of the delimited int strings that specify a vertex and its attributes
	// 
	// returns:
	// whether the vertex is valid
	// and the indices in the out parameters
	static bool getVertexAttributes( QString objFaceVertexToken,
		int* pPositionIndex, int* pTextureCoordinateIndex, int* pNormalIndex );
};

#endif // OBJ_LOADER_H
