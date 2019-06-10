#include "HostDeviceData.h"
import Shading;

// TODO: Figure out how to use unbounded arrays in Slang
#define MAX_BINDLESS_MATERIALS 5000

Texture2D gBindlessMaterialBaseColor[MAX_BINDLESS_MATERIALS];
Texture2D gBindlessMaterialSpecular[MAX_BINDLESS_MATERIALS];
Texture2D gBindlessMaterialEmissive[MAX_BINDLESS_MATERIALS];
Texture2D gBindlessMaterialNormalMap[MAX_BINDLESS_MATERIALS];

// TODO: Handle sampler state, texture/constant flag

void applyNormalMapBindless<L:ITextureSampler>(MaterialData m, inout ShadingData sd, L lod, uint materialID)
{
    uint mapType = EXTRACT_NORMAL_MAP_TYPE(m.flags);
    if(mapType == NormalMapUnused) return;

    float3 mapN = lod.sampleTexture(gBindlessMaterialNormalMap[materialID], m.resources.samplerState, sd.uv).xyz;
    switch(mapType)
    {
    case NormalMapRGB:
        mapN = RgbToNormal(mapN);
        break;
    case NormalMapRG:
        mapN = RgToNormal(mapN.rg);
        break;
    default:
        return;
    }

    // Apply the transformation. Everything is normalized already
    sd.N = sd.T * mapN.x + sd.B * mapN.y + sd.N * mapN.z;
    sd.B = normalize(sd.B - sd.N * dot(sd.B, sd.N));
    sd.T = normalize(cross(sd.B, sd.N));
}

ShadingData prepareShadingDataBindless(VertexOut v, MaterialData m, float3 camPosW, uint materialID)
{
    ImplicitLodTextureSampler lod = { };

    ShadingData sd = initShadingData();

#ifdef _MS_STATIC_MATERIAL_FLAGS
    m.flags = _MS_STATIC_MATERIAL_FLAGS;
#endif

    // Sample the diffuse texture and apply the alpha test
    float4 baseColor = sampleTexture(gBindlessMaterialBaseColor[materialID], m.resources.samplerState, v.texC, m.baseColor, EXTRACT_DIFFUSE_TYPE(m.flags), lod);
    sd.opacity = m.baseColor.a;
    applyAlphaTest(m.flags, baseColor.a, m.alphaThreshold, v.posW);

    sd.posW = v.posW;
    sd.uv = v.texC;
    sd.V = normalize(camPosW - v.posW);
    sd.N = normalize(v.normalW);

    sd.B = normalize(v.bitangentW - sd.N * (dot(v.bitangentW, sd.N)));
    sd.T = normalize(cross(sd.B, sd.N));

    // Sample the spec texture
    bool sampleOcclusion = EXTRACT_OCCLUSION_MAP(m.flags) > 0;
    float4 spec = sampleTexture(gBindlessMaterialSpecular[materialID], m.resources.samplerState, v.texC, m.specular, EXTRACT_SPECULAR_TYPE(m.flags), lod);
    if (EXTRACT_SHADING_MODEL(m.flags) == ShadingModelMetalRough)
    {
        // R - Occlusion; G - Roughness; B - Metalness
        sd.diffuse = lerp(baseColor.rgb, float3(0), spec.b);

        // UE4 uses 0.08 multiplied by a default specular value of 0.5 as a base, hence the 0.04
        sd.specular = lerp(float3(0.04f), baseColor.rgb, spec.b);
        sd.linearRoughness = spec.g;

        if (sampleOcclusion) sd.occlusion = spec.r;
    }
    else // if (EXTRACT_SHADING_MODEL(m.flags) == ShadingModelSpecGloss)
    {
        sd.diffuse = baseColor.rgb;
        sd.specular = spec.rgb;
        sd.linearRoughness = 1 - spec.a;

        if(sampleOcclusion)
        {
            sd.occlusion = sampleTexture(m.resources.occlusionMap, m.resources.samplerState, v.texC, 1, ChannelTypeTexture, lod);
        }
    }

    sd.linearRoughness = max(0.08, sd.linearRoughness); // Clamp the roughness so that the BRDF won't explode
    sd.roughness = sd.linearRoughness * sd.linearRoughness;
    sd.emissive = sampleTexture(gBindlessMaterialEmissive[materialID], m.resources.samplerState, v.texC, float4(m.emissive, 1), EXTRACT_EMISSIVE_TYPE(m.flags), lod).rgb;
    sd.IoR = m.IoR;
    sd.doubleSidedMaterial = EXTRACT_DOUBLE_SIDED(m.flags);

#define channel_type(extract) (extract(m.flags) ? ChannelTypeTexture : ChannelTypeUnused)
    sd.lightMap = sampleTexture(m.resources.lightMap, m.resources.samplerState, v.lightmapC, 1, channel_type(EXTRACT_LIGHT_MAP), lod).rgb;
    sd.height = sampleTexture(m.resources.heightMap, m.resources.samplerState, v.texC, 1, channel_type(EXTRACT_HEIGHT_MAP), lod).xy;
    sd.height = sd.height * m.heightScaleOffset.x + m.heightScaleOffset.y;
#undef channel_type

    applyNormalMapBindless(m, sd, lod, materialID);
    sd.NdotV = dot(sd.N, sd.V);

    // Flip the normal if it's backfacing
    if(sd.NdotV <= 0 && sd.doubleSidedMaterial)
    {
        sd.N = -sd.N;
        sd.NdotV = -sd.NdotV;
    }

    return sd;
}

