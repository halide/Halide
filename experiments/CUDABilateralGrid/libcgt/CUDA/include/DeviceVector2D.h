#ifndef DEVICE_ARRAY_2D_H
#define DEVICE_ARRAY_2D_H

// TODO: can this be unified directly with CUDAVector?
template< typename T >
struct DeviceVector2D
{
	T* pointer;
	int width;
	int height;
	size_t pitch;

	__host__
	DeviceVector2D( T* _pointer, int _width, int _height, size_t _pitch );

	__device__
	T* getRowPointer( int y );

	__device__
	T& operator () ( int x, int y );

	__device__
	T& operator () ( int2 xy );

	__device__
	T& operator () ( uint2 xy );
};

template< typename T >
DeviceVector2D< T >::DeviceVector2D( T* _pointer, int _width, int _height, size_t _pitch ) :

	pointer( _pointer ),
	width( _width ),
	height( _height ),
	pitch( _pitch )

{

}

template< typename T >
T* DeviceVector2D< T >::getRowPointer( int y )
{
	// TODO: char --> ubyte
	char* p = reinterpret_cast< char* >( pointer );
	
	// TODO: switch pointer arithmetic to array indexing?
	return reinterpret_cast< T* >( p + y * pitch );
}

template< typename T >
T& DeviceVector2D< T >::operator () ( int x, int y )
{
	return getRowPointer( y )[ x ];
}

template< typename T >
T& DeviceVector2D< T >::operator () ( int2 xy )
{
	return getRowPointer( xy.y )[ xy.x ];
}

template< typename T >
T& DeviceVector2D< T >::operator () ( uint2 xy )
{
	return getRowPointer( xy.y )[ xy.x ];
}

#endif // DEVICE_ARRAY_2D_H
