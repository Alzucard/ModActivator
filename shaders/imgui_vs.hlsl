// Vertex shader of Dear ImGui's DirectX 12 backend, verbatim from
// backends/imgui_impl_dx12.cpp at ocornut/imgui 368123a (v1.91.5-docking),
// where it is compiled at runtime with D3DCompile. MIT License, see
// licenses/IMGUI-LICENSE.txt. Precompiled into src/gui/imgui_dx12_shaders.h
// as g_imguiVS; the regeneration command is in that header.
cbuffer vertexBuffer : register(b0)
{
  float4x4 ProjectionMatrix;
};
struct VS_INPUT
{
  float2 pos : POSITION;
  float4 col : COLOR0;
  float2 uv  : TEXCOORD0;
};

struct PS_INPUT
{
  float4 pos : SV_POSITION;
  float4 col : COLOR0;
  float2 uv  : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input)
{
  PS_INPUT output;
  output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));
  output.col = input.col;
  output.uv  = input.uv;
  return output;
}
