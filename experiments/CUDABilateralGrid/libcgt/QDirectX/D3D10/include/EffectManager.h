#ifndef EFFECT_MANAGER_H
#define EFFECT_MANAGER_H

#include <d3d10_1.h>
#include <d3d10.h>
#include <d3dx10.h>
#include <QHash>
#include <QString>

class EffectManager
{
public:

	EffectManager( ID3D10Device* pDevice );
	virtual ~EffectManager();

	bool loadFromFile( QString name, QString filename );
	bool loadFromBinaryFile( QString name, QString filename );
	
	ID3D10Effect* getEffect( QString name );

private:

	ID3D10Device* m_pDevice;
	QHash< QString, ID3D10Effect* > m_effects;

};

#endif // EFFECT_MANAGER_H
