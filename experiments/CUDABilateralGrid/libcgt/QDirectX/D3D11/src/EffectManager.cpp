#include "EffectManager.h"

#include <QFile>
#include <D3Dcompiler.h>

EffectManager::EffectManager( ID3D11Device* pDevice ) :
	m_pDevice( pDevice )
{
	m_pDevice->AddRef();
}

// virtual
EffectManager::~EffectManager()
{
	foreach( ID3DX11Effect* effect, m_effects )
	{
		effect->Release();
	}

	m_pDevice->Release();
}

bool EffectManager::loadFromFile( QString name, QString filename )
{
	ID3DX11Effect* pEffect = NULL;
	ID3DBlob* pCompiledEffect = NULL;
	ID3DBlob* pErrorMessages = NULL;

	UINT shadeFlags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR;

#if _DEBUG
	shadeFlags |= D3DCOMPILE_DEBUG;
	shadeFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	// compile effect	
	HRESULT hr = D3DX11CompileFromFile
	(
		filename.utf16(), // filename
		NULL, // #defines
		NULL, // includes
		NULL, // function name,
		"fx_5_0", // profile
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
		hr = D3DX11CreateEffectFromMemory
		(
			pCompiledEffect->GetBufferPointer(),
			pCompiledEffect->GetBufferSize(),
			0, // flags
			m_pDevice,
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
			ID3DX11Effect* pEffect;
			QByteArray data = file.readAll();
			HRESULT hr = D3DX11CreateEffectFromMemory
			(
				reinterpret_cast< void* >( data.data() ),
				data.size(),
				0,
				m_pDevice,
				&pEffect
			);
			if( SUCCEEDED( hr ) )
			{
				m_effects[ name ] = pEffect;
				return true;
			}
		}
	}

	return false;
}

ID3DX11Effect* EffectManager::getEffect( QString name )
{
	return m_effects[ name ];
}
