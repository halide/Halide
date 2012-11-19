#ifndef Q_REFERENCE_HASH_H
#define Q_REFERENCE_HASH_H

// Include this file if you want to put References inside QHash hash tables.

#include <QHash>

template< typename T >
uint qHash( const Reference< T >& ref )
{
	const T* ptr = ref;
	return qHash( ptr );
}

#endif // Q_REFERENCE_HASH_H
