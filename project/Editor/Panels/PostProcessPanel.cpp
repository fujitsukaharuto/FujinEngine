#include "PostProcessPanel.h"
#include "Engine/Renderer/PostProcess/PostProcessPass.h"
#include "imgui.h"

namespace Fujin {

void PostProcessPanel::Draw(PostProcessPass* pp) {
    if (!ImGui::Begin("Post Process")) { ImGui::End(); return; }

    if (!pp) {
        ImGui::TextDisabled("レンダラー未接続");
        ImGui::End();
        return;
    }

    // ── Bloom ──────────────────────────────────────────────────────────────
    // SetNextItemAllowOverlap() を先に呼ぶことで、ヘッダー上に重ねた
    // チェックボックスが先にクリックを受け取れるようにする
    ImGui::SetNextItemAllowOverlap();
    bool bloomOpen = ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    ImGui::Checkbox("##bloom_en", &pp->BloomEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bloom 有効/無効");

    if (bloomOpen) {
        ImGui::BeginDisabled(!pp->BloomEnabled);
        ImGui::Indent(12.0f);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Threshold");
        ImGui::SameLine(100.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##bloom_thresh", &pp->BloomThreshold, 0.0f, 5.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("この輝度以上の画素をブルームに取り込む (低いほど広がりやすい)");

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Strength");
        ImGui::SameLine(100.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##bloom_str", &pp->BloomStrength, 0.0f, 1.0f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("ブルームの合成強度");

        ImGui::Unindent(12.0f);
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    // ── Tonemap ────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Tonemap", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(12.0f);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Exposure");
        ImGui::SameLine(100.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##exposure", &pp->Exposure, 0.1f, 8.0f, "%.2f EV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("HDR 画像全体の露出倍率");

        ImGui::Unindent(12.0f);
    }

    ImGui::Spacing();

    // ── SSAO ───────────────────────────────────────────────────────────
    ImGui::SetNextItemAllowOverlap();
    bool ssaoOpen = ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    ImGui::Checkbox("##ssao_en", &pp->SSAOEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("SSAO 有効/無効");

    if (ssaoOpen) {
        ImGui::Indent(12.0f);
        ImGui::TextDisabled("有効時: マテリアルAO × SSAOマップ");
        ImGui::TextDisabled("無効時: マテリアルAOのみ");
        ImGui::Unindent(12.0f);
    }

    ImGui::Spacing();

    // ── FXAA ───────────────────────────────────────────────────────────
    ImGui::SetNextItemAllowOverlap();
    bool fxaaOpen = ImGui::CollapsingHeader("FXAA", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    ImGui::Checkbox("##fxaa_en", &pp->FXAAEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("FXAA アンチエイリアス 有効/無効");

    if (fxaaOpen) {
        ImGui::Indent(12.0f);
        ImGui::TextDisabled("簡易 Lottes FXAA (4近傍エッジ検出)");
        ImGui::Unindent(12.0f);
    }

    ImGui::End();
}

} // namespace Fujin
