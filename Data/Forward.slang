import Shading;

#if defined(MULTI_DRAW) || defined(BINDLESS_CONSTANTS)
import BindlessVS;
#else
import DefaultVS;
#endif

#ifdef BINDLESS_MATERIAL
import BindlessMaterial;
#endif

[[vk::push_constant]]
cbuffer PushConstantBuffer
{
    uint gDrawID;
};

struct MainVSOut
{
    VertexOut defaultVSOut;
    uint drawID : DRAW_ID;
};

uint GetDrawID(VertexIn vIn)
{
#if defined(MULTI_DRAW)
    // Use gl_InstanceID (args.baseInstance + 0) to pass drawID
    return vIn.instanceID;
#else
    // Use push constants (per-draw) to pass drawID
    return gDrawID;
#endif
}

MainVSOut MainVS(VertexIn vIn)
{
    const uint drawID = GetDrawID(vIn);

    MainVSOut out;
#if defined(MULTI_DRAW) || defined(BINDLESS_CONSTANTS)
    out.defaultVSOut = bindlessVS(vIn, drawID); 
#else
    out.defaultVSOut = defaultVS(vIn);
#endif
    out.drawID = drawID;

    return out;
}

float4 MainPS(MainVSOut mainVSOut) : SV_TARGET
{
    VertexOut vOut = mainVSOut.defaultVSOut;
    const uint materialID = mainVSOut.drawID; // Assumes identity drawID - materialID mapping

#ifdef BINDLESS_MATERIAL
    ShadingData sd = prepareShadingDataBindless(vOut, gMaterial, gCamera.posW, materialID);
#else
    ShadingData sd = prepareShadingData(vOut, gMaterial, gCamera.posW);
#endif

    ShadingResult sr = evalMaterial(sd, gLights[0], 1);

    float4 color = 0;
    color.rgb += sr.color.rgb;
    color.rgb += sd.emissive;
    color.a = 1;

    // Test if drawID is hooked up correctly
    if (mainVSOut.drawID == 5)
    {
        color.r = 1.0;
    }

    return color;
}
