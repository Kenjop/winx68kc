Texture2D       g_texture : register(t0);		// 基本色
SamplerState    g_sampler : register(s0);		// サンプラ

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

float4 ps_0(PS_INPUT input) : SV_TARGET
{
	return g_texture.Sample(g_sampler, input.uv);
}
