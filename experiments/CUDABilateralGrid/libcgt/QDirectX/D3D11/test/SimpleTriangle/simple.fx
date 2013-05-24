RasterizerState solidRS
{
	FrontCounterClockwise = TRUE;
	CullMode = NONE;
	FillMode = SOLID;
};

DepthStencilState depthTestEnabled
{
	DepthEnable = TRUE;
	StencilEnable = FALSE;
	DepthFunc = LESS;
	DepthWriteMask = ALL;
};

BlendState alphaBlendDisabled
{
	BlendEnable[ 0 ] = FALSE;
};

struct VS_IN
{
    float4 world : POSITION;
    float4 color : COLOR;
};

struct PS_IN
{
    float4 clip : SV_POSITION;
    float4 color : COLOR;
};

matrix pvw;

PS_IN vs( VS_IN input )
{
    PS_IN output = ( PS_IN )0;
    
    //output.clip = mul( pvw, input.world );
    output.clip = mul( input.world, pvw );
    output.color = input.color;
    
    return output;
}

float4 ps( PS_IN input ) : SV_Target
{
	return input.color;
}

technique11 t0
{
	pass p0
	{
		SetRasterizerState( solidRS );

		SetVertexShader( CompileShader( vs_5_0, vs() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_5_0, ps() ) );

		SetDepthStencilState( depthTestEnabled, 0 );
		SetBlendState( alphaBlendDisabled, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
	}
}
