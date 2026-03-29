Texture2D gDiffuseMap : register(t0);
SamplerState gsamLinear : register(s0);

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
};

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

float4 PS(VertexOut pin) : SV_Target
{
    float3 N = normalize(pin.NormalW);

    float3 lightDir = normalize(float3(0.577f, -0.577f, 0.577f));
    float ndotl = saturate(dot(N, -lightDir));

    float4 texColor = gDiffuseMap.Sample(gsamLinear, pin.TexC);

    float3 ambient = gAmbientLight.rgb * texColor.rgb;
    float3 diffuse = ndotl * texColor.rgb;

    return float4(ambient + diffuse, 1.0f);
}
