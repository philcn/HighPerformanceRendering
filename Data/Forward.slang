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

MainVSOut MainVS(VertexIn vIn)
{
    MainVSOut out;
#if defined(MULTI_DRAW)
    out.defaultVSOut = bindlessVS(vIn, 0); // TODO: Figure out the equivalent of gl_DrawID / gl_DrawIndex
#elif defined(BINDLESS_CONSTANTS)
    out.defaultVSOut = bindlessVS(vIn, gDrawID);
#else
    out.defaultVSOut = defaultVS(vIn);
#endif
    out.drawID = gDrawID;

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
