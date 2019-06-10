import Shading;
import DefaultVS;
import BindlessVS;

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
    ShadingData sd = prepareShadingData(vOut, gMaterial, gCamera.posW);

    ShadingResult sr = evalMaterial(sd, gLights[0], 1);

    float4 color = 0;
    color.rgb += sr.color.rgb;
    color.rgb += sd.emissive;
    color.a = 1;

    if (mainVSOut.drawID == 5)
    {
        color.r = 1.0;
    }

    return color;
}
