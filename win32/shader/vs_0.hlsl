cbuffer ConstantBuffer : register(b0)
{
	matrix wp;
};

struct VS_INPUT
{
	float4 position : POSITION;
	float2 uv : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

VS_OUTPUT vs_0(VS_INPUT input)
{
	VS_OUTPUT output;
	output.position = mul(input.position, wp);
	output.uv = input.uv;
	return output;
}
