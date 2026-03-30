Texture2D gDiffuseMap : register(t0);
SamplerState gsamLinear : register(s0);

struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
};

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
    Light gLights[16];
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

float CalcAttenuation(float d, float start, float end)
{
    return saturate((end - d) / (end - start));
}

float3 ComputeDirectionalLight(Light L, float3 N, float3 baseColor)
{
    float3 lightDir = normalize(-L.Direction);
    float ndotl = saturate(dot(N, lightDir));
    return L.Strength * ndotl * baseColor;
}

float3 ComputePointLight(Light L, float3 posW, float3 N, float3 baseColor)
{
    float3 toLight = L.Position - posW;
    float d = length(toLight);

    if (d > L.FalloffEnd)
        return float3(0.0f, 0.0f, 0.0f);

    float3 lightVec = toLight / d;
    float ndotl = saturate(dot(N, lightVec));
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);

    return L.Strength * ndotl * att * baseColor;
}

float4 PS(VertexOut pin) : SV_Target
{
    float3 N = normalize(pin.NormalW);

    float2 uv = pin.TexC;

    if (gRoughness < 0.06f)
    {
        uv.x += gTotalTime * 0.08f;
        uv.y += gTotalTime * 0.03f;
    }

    float4 texColor = gDiffuseMap.Sample(gsamLinear, uv);
    float3 baseColor = texColor.rgb * gDiffuseAlbedo.rgb;

    float3 ambient = gAmbientLight.rgb * baseColor;

    float3 lighting = float3(0.0f, 0.0f, 0.0f);
    lighting += ComputeDirectionalLight(gLights[0], N, baseColor);
    lighting += ComputePointLight(gLights[1], pin.PosW, N, baseColor);
    lighting += ComputePointLight(gLights[2], pin.PosW, N, baseColor);
    lighting += ComputePointLight(gLights[3], pin.PosW, N, baseColor);
    lighting += ComputePointLight(gLights[4], pin.PosW, N, baseColor);
    lighting += ComputePointLight(gLights[5], pin.PosW, N, baseColor);

    return float4(ambient + lighting, texColor.a * gDiffuseAlbedo.a);
}