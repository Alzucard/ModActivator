// Pixel shader of Dear ImGui's DirectX 12 backend, verbatim from
// backends/imgui_impl_dx12.cpp at ocornut/imgui 368123a (v1.91.5-docking),
// where it is compiled at runtime with D3DCompile. MIT License, see
// licenses/IMGUI-LICENSE.txt. Precompiled into src/gui/imgui_dx12_shaders.h
// as g_imguiPS; the regeneration command is in that header.
struct PS_INPUT
{
  float4 pos : SV_POSITION;
  float4 col : COLOR0;
  float2 uv  : TEXCOORD0;
};
SamplerState sampler0 : register(s0);
Texture2D texture0 : register(t0);

float4 main(PS_INPUT input) : SV_Target
{
  float4 out_col = input.col * texture0.Sample(sampler0, input.uv);
  return out_col;
}
