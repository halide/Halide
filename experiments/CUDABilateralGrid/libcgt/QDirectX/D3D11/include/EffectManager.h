#ifndef EFFECT_MANAGER_H
#define EFFECT_MANAGER_H

#include <D3D11.h>
#include <d3dx11.h>
#include <d3dx11effect.h>
#include <QHash>
#include <QString>

class EffectManager
{
public:

	EffectManager( ID3D11Device* pDevice );
	virtual ~EffectManager();

	// Load the effect from a source file in "filename"
	// and assign it the name "name"
	bool loadFromFile( QString name, QString filename );

	// Load the binary effect from a pre-compiled file in "filename"
	// and assign it the name "name"
	bool loadFromBinaryFile( QString name, QString filename );
	
	ID3DX11Effect* getEffect( QString name );

private:

	ID3D11Device* m_pDevice;
	QHash< QString, ID3DX11Effect* > m_effects;

};

#endif // EFFECT_MANAGER_H
