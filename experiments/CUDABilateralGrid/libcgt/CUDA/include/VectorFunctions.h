#ifndef VECTOR_FUNCTIONS_H
#define VECTOR_FUNCTIONS_H

// makes an int2 out of a short2
__host__ __device__ __inline
int2 make_int2( short2 s )
{
	return make_int2( s.x, s.y );
}

// flips x and y
__host__ __device__ __inline
int2 yx( int2 xy )
{
	return make_int2( xy.y, xy.x );	
}

__host__ __device__ __inline
float3 getXYZ( float4 f )
{
	return make_float3( f.x, f.y, f.z );
}

__host__ __device__ __inline
float normL1( float2 v )
{
	return abs( v.x ) + abs( v.y );
}

__host__ __device__ __inline
float normL1( float3 v )
{
	return abs( v.x ) + abs( v.y ) + abs( v.z );
}

#endif // VECTOR_FUNCTIONS_H
