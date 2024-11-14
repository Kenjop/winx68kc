Texture2D       g_texture : register(t0);		// ��{�F
SamplerState    g_sampler : register(s0);		// �T���v��

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

float4 ps_0(PS_INPUT input) : SV_TARGET
{
	return g_texture.Sample(g_sampler, input.uv);
}
