#include "VertexAttrib.h"
__import DefaultVS;
__import ShaderCommon;

struct DrawConstants
{
    float4x4 worldMat;              // Per-instance world transforms
    float4x4 prevWorldMat;          // Previous frame world transforms
    float3x4 worldInvTransposeMat;  // Per-instance matrices for transforming normals
    uint32_t drawId;                // Zero-based order/ID of Mesh Instances drawn per SceneRenderer::renderScene call.
    uint32_t meshId;
    uint32_t pad[2];
};

// Don't use StructuredBuffer because it's buggy
RWStructuredBuffer<DrawConstants> gDrawConstants;

float4x4 getWorldMatBindless(VertexIn vIn, uint drawID)
{
    return gDrawConstants[drawID].worldMat;
}

float3x3 getWorldInvTransposeMatBindless(VertexIn vIn, uint drawID)
{
    return (float3x3)gDrawConstants[drawID].worldInvTransposeMat;
}

VertexOut bindlessVS(VertexIn vIn, uint drawID)
{
    VertexOut vOut;
    float4x4 worldMat = getWorldMatBindless(vIn, drawID);
    float4 posW = mul(vIn.pos, worldMat);
    vOut.posW = posW.xyz;
    vOut.posH = mul(posW, gCamera.viewProjMat);

#ifdef HAS_TEXCRD
    vOut.texC = vIn.texC;
#else
    vOut.texC = 0;
#endif

#ifdef HAS_COLORS
    vOut.colorV = vIn.color;
#else
    vOut.colorV = 0;
#endif

#ifdef HAS_NORMAL
    vOut.normalW = mul(vIn.normal, getWorldInvTransposeMatBindless(vIn, drawID)).xyz;
#else
    vOut.normalW = 0;
#endif

#ifdef HAS_BITANGENT
    vOut.bitangentW = mul(vIn.bitangent, (float3x3)getWorldMatBindless(vIn, drawID));
#else
    vOut.bitangentW = 0;
#endif

#ifdef HAS_LIGHTMAP_UV
    vOut.lightmapC = vIn.lightmapC;
#else
    vOut.lightmapC = 0;
#endif

#ifdef HAS_PREV_POSITION
    float4 prevPos = vIn.prevPos;
#else
    float4 prevPos = vIn.pos;
#endif
    float4 prevPosW = mul(prevPos, gDrawConstants[drawID].prevWorldMat);
    vOut.prevPosH = mul(prevPosW, gCamera.prevViewProjMat);

#ifdef _SINGLE_PASS_STEREO
    vOut.rightEyePosS = mul(posW, gCamera.rightEyeViewProjMat).x;
    vOut.viewportMask = 0x00000001;
    vOut.renderTargetIndex = 0;
#endif

  return vOut;
}
