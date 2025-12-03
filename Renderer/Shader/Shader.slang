struct MatrixUB
{
    float4x4 world;
    float4x4 view;
    float4x4 proj;
    float4x4 WVP;
};
ConstantBuffer<MatrixUB> MUB;

struct VSInput
{
    float3 inPos : POSITION;
    float3 inCol : COLOR;
    float2 inUV : TEXCOORD0;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float3 col : COLOR;
    float2 UV  : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    VSOutput output;
    output.pos = mul(MUB.WVP, float4(input.inPos, 1.0));
    output.col = input.inCol;
    output.UV  = input.inUV;
    return output;
}

Sampler2D texture;

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET
{
    float4 texColor = texture.Sample(vertIn.UV);
    return texColor;
}