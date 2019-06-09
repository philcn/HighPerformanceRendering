import ShaderCommon;
import Shading;
import DefaultVS;

[[vk::push_constant]]
cbuffer PushConstantBuffer
{
    uint gDrawID;
};

// For debug purpose
struct MainVSOut
{
    VertexOut defaultVSOut;
    float3 posM : MODEL_POSITION;
    uint drawID : DRAW_ID;
};

MainVSOut MainVS(VertexIn vIn)
{
    MainVSOut out;
    out.defaultVSOut = defaultVS(vIn);
    out.posM = vIn.pos.xyz;
    out.drawID = gDrawID + vIn.instanceID;

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
