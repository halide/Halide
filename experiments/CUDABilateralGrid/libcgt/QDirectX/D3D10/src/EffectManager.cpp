#include "EffectManager.h"

#include <QFile>

EffectManager::EffectManager( ID3D10Device* pDevice ) :
	m_pDevice( pDevice )
{
	m_pDevice->AddRef();
}

// virtual
EffectManager::~EffectManager()
{
	foreach( ID3D10Effect* effect, m_effects )
	{
		effect->Release();
	}

	m_pDevice->Release();
}

bool EffectManager::loadFromFile( QString name, QString filename )
{
	ID3D10Effect* pEffect;
	ID3D10Blob* pCompiledEffect;
	ID3D10Blob* pErrorMessages;

	UINT shadeFlags = 0;

	shadeFlags |= D3D10_SHADER_PACK_MATRIX_COLUMN_MAJOR;

#if _DEBUG
	shadeFlags |= D3D10_SHADER_DEBUG;
	shadeFlags |= D3D10_SHADER_SKIP_OPTIMIZATION;
#endif

	// compile effect	
	HRESULT hr = D3DX10CompileFromFile
	(
		// reinterpret_cast< LPCWSTR >( filename.toUtf8().constData() ), // filename
		// filename,
		filename.utf16(), // filename
		NULL, // #defines
		NULL, // includes
		NULL, // function name,
		"fx_4_0", // profile
		shadeFlags, // shade flags
		0, // effect flags
		NULL, // thread pump
		&pCompiledEffect,
		&pErrorMessages,
		NULL // return value
	);

	// actually construct the effect
	if( SUCCEEDED( hr ) )
	{
		hr = D3D10CreateEffectFromMemory
		(
			pCompiledEffect->GetBufferPointer(),
			pCompiledEffect->GetBufferSize(),
			0, // flags
			m_pDevice,
			NULL,
			&pEffect
		);
		if( SUCCEEDED( hr ) )
		{
			m_effects[ name ] = pEffect;
		}
	}
	else
	{
		printf( "%s\n", pErrorMessages->GetBufferPointer() );
	}

	if( pErrorMessages != NULL )
	{
		pErrorMessages->Release();
	}
	if( pCompiledEffect != NULL )
	{
		pCompiledEffect->Release();
	}

	return SUCCEEDED( hr );
}

bool EffectManager::loadFromBinaryFile( QString name, QString filename )
{
	QFile file( filename );
	if( file.exists() )
	{
		if( file.open( QIODevice::ReadOnly ) )
		{
			ID3D10Effect* pEffect;
			QByteArray data = file.readAll();
			HRESULT hr = D3D10CreateEffectFromMemory( reinterpret_cast< void* >( data.data() ), data.size(), 0, m_pDevice, NULL, &pEffect );
			if( SUCCEEDED( hr ) )
			{
				m_effects[ name ] = pEffect;
				return true;
			}
		}
	}

	return false;
}

ID3D10Effect* EffectManager::getEffect( QString name )
{
	return m_effects[ name ];
}
