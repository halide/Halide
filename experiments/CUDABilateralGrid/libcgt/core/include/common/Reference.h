#ifndef REFERENCE_H
#define REFERENCE_H

// A slighly modified version of Eugene Hsu's magical reference-counted pointer.

// You should initialize these with new and then forget about it.
// Memory will be automatically deallocated, etc.
//
// Good: Reference<Object> p = new Object();
// Good: Reference<Object> q(p);
// Bad:  delete p;
// Bad:  Reference<Object> p = &object;
//
// This has been (recently) extended to handle inherited classes (via
// templated cast operator).  E.g., Reference<Base> b = new Sub();

#include "common/BasicTypes.h"

template< typename T >
class Reference
{
public:

	// Constructors / Destructors
	Reference( T* p = NULL, uint* refcount = NULL );
	Reference( const Reference< T >& other );
	virtual ~Reference();
	Reference< T >& operator = ( const Reference& other );

	// Accessors
	T* operator -> ();
	const T* operator -> () const;

	operator T* ();
	operator const T* () const;

	T& operator * ();
	const T& operator * () const;

	bool operator ! () const;

	// When you really need a pointer
	T* pointer();

	// pointer equality
	bool operator == ( const Reference& other ) const;

	bool isNull();
	bool notNull();

	// const cast
	operator Reference< const T > () const;

	// this is a cast to superclass S
	// enables inheritance to work, etc.
	template< typename S >
	operator Reference< S > () const;

private:

	T* m_p;
	mutable uint* m_refcount;

	void destroy();	
};

template< typename T >
uint qHash( const Reference< T >& ref );

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

template< typename T >
Reference< T >::Reference( T* p, uint* refcount )
{
	m_p = p;
	m_refcount = refcount;

	if( m_refcount == NULL )
	{
		m_refcount = new uint( 1 );
	}
	else
	{
		++( *m_refcount );
	}
}

template< typename T >
Reference< T >::Reference( const Reference< T >& other ) :
	
m_p( other.m_p ),
m_refcount( other.m_refcount )

{
	++( *m_refcount );
}

// virtual
template< typename T >
Reference< T >::~Reference()
{
	destroy();
}

template< typename T >
Reference< T >& Reference< T >::operator = ( const Reference& other )
{
	if( &other != this )
	{
		destroy();
		m_p = other.m_p;
		m_refcount = other.m_refcount;
		++( *m_refcount );
	}
	return *this;
}

template< typename T >
T* Reference< T >::operator -> ()
{
	return m_p;
}

template< typename T >
const T* Reference< T >::operator -> () const
{
	return m_p;
}

template< typename T >
Reference< T >::operator T* ()
{
	return m_p;
}

template< typename T >
Reference< T >::operator const T* () const
{
	return m_p;
}

template< typename T >
T& Reference< T >::operator * ()
{
	return( *m_p );
}

template< typename T >
const T& Reference< T >::operator * () const
{
	return( *m_p );
}

template< typename T >
bool Reference< T >::operator ! () const
{
	return m_p == NULL;
}

template< typename T >
T* Reference< T >::pointer()
{
	return m_p;
}

template< typename T >
bool Reference< T >::operator == ( const Reference& other ) const
{
	return( m_p == other.m_p );
}

template< typename T >
bool Reference< T >::isNull()
{
	return( m_p == NULL );
}

template< typename T >
bool Reference< T >::notNull()
{
	return( m_p != NULL );
}

template< typename T >
Reference< T >::operator Reference< const T > () const
{
	return Reference< const T >( m_p, m_refcount );
}

template< typename T >
template< typename S >
Reference< T >::operator Reference< S > () const
{	
	return Reference< S >( m_p, m_refcount );
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

template< typename T >
void Reference< T >::destroy()
{
	--( *m_refcount );
	if( *m_refcount == 0 )
	{
		delete m_refcount;
		delete m_p;
	}
}

#endif // REFERENCE_H
