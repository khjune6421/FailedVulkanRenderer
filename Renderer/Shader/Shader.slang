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
    float2 inPos;
    float3 inCol;
};

struct VSOutput
{
    float4 pos : SV_Position;
    float3 col;
};

[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    VSOutput output;
    output.pos = mul(MUB.WVP, float4(input.inPos, 0.0, 1.0));
    output.col = input.inCol;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET
{
    return float4(vertIn.col, 1.0);
}