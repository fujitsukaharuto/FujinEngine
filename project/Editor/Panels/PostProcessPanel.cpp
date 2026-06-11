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
        ImGui::TextUnformatted("Curve");
        ImGui::SameLine(100.0f);
        ImGui::SetNextItemWidth(-1.0f);
        const char* curves[] = { "ACES", "AgX" };
        ImGui::Combo("##tonemapMode", &pp->TonemapMode, curves, 2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("トーンマップカーブ。AgX は現代 UE5 寄りの色域・コントラスト");

        ImGui::BeginDisabled(pp->AutoExposureEnabled);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Exposure");
        ImGui::SameLine(100.0f);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##exposure", &pp->Exposure, 0.1f, 8.0f, "%.2f EV");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("HDR 画像全体の露出倍率 (Auto Exposure 有効時は無効)");
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Checkbox("Auto Exposure", &pp->AutoExposureEnabled);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("シーン輝度に応じて露出を自動調整 (アイアダプテーション)");
        if (pp->AutoExposureEnabled) {
            ImGui::Indent(12.0f);
            ImGui::SliderFloat("Key##ae",   &pp->AutoExposureKey,   0.02f, 0.5f, "%.3f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("目標中間輝度 (大きいほど明るく)");
            ImGui::SliderFloat("Speed##ae", &pp->AutoExposureSpeed, 0.005f, 0.5f, "%.3f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("順応の速さ (大きいほど即座に追従)");
            ImGui::SliderFloat("Comp##ae",  &pp->ExposureCompensation, 0.1f, 4.0f, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("自動露出への手動バイアス");
            ImGui::SliderFloat("Min##ae",   &pp->AutoExposureMin, 0.01f, 1.0f, "%.2f");
            ImGui::SliderFloat("Max##ae",   &pp->AutoExposureMax, 1.0f, 16.0f, "%.1f");
            ImGui::Unindent(12.0f);
        }

        ImGui::Unindent(12.0f);
    }

    ImGui::Spacing();

    // ── Lens (vignette + chromatic aberration) ───────────────────────────────
    if (ImGui::CollapsingHeader("Lens", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(12.0f);

        ImGui::TextUnformatted("Vignette");
        ImGui::SliderFloat("Intensity##vig", &pp->VignetteIntensity, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("四隅の暗さ (0 = 無効)");
        ImGui::BeginDisabled(pp->VignetteIntensity <= 0.0f);
        ImGui::SliderFloat("Softness##vig", &pp->VignetteSoftness, 0.0f, 0.95f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("暗くなり始める半径 (小さいほど中心寄り)");
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::TextUnformatted("Chromatic Aberration");
        ImGui::SliderFloat("Amount##ca", &pp->ChromaticAberration, 0.0f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("レンズの色収差 (RGB の横ずれ, 0 = 無効)");

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

    // ── Contact Shadows (screen-space, directional light) ───────────────
    ImGui::SetNextItemAllowOverlap();
    bool csOpen = ImGui::CollapsingHeader("Contact Shadows", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    ImGui::Checkbox("##cs_en", &pp->ContactShadowsEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("スクリーンスペース・コンタクトシャドウ 有効/無効");

    if (csOpen) {
        ImGui::Indent(12.0f);
        ImGui::BeginDisabled(!pp->ContactShadowsEnabled);
        ImGui::SliderFloat("Length##cs",    &pp->ContactShadowLength,    0.02f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("レイの長さ (ワールド単位、大きいほど遠くまで遮蔽)");
        ImGui::SliderFloat("Strength##cs",  &pp->ContactShadowStrength,  0.0f,  1.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("遮蔽時の暗さ");
        ImGui::SliderInt("Steps##cs",       &pp->ContactShadowSteps,     2,     32);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("マーチ回数 (多いほど高品質・高コスト)");
        ImGui::SliderFloat("Thickness##cs", &pp->ContactShadowThickness, 0.02f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("遮蔽物の厚み判定窓");
        ImGui::EndDisabled();
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

    // ── TAA ────────────────────────────────────────────────────────────
    ImGui::SetNextItemAllowOverlap();
    bool taaOpen = ImGui::CollapsingHeader("TAA", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    ImGui::Checkbox("##taa_en", &pp->TaaEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Temporal AA (深度再投影) 有効/無効");

    if (taaOpen) {
        ImGui::Indent(12.0f);
        ImGui::SliderFloat("History Blend", &pp->TaaHistoryBlend, 0.5f, 0.97f, "%.2f");
        ImGui::TextDisabled("動く物体はゴースト（モーションベクタ未対応）");
        ImGui::Unindent(12.0f);
    }

    // ── SSR ────────────────────────────────────────────────────────────
    ImGui::SetNextItemAllowOverlap();
    bool ssrOpen = ImGui::CollapsingHeader("SSR", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    ImGui::Checkbox("##ssr_en", &pp->SsrEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("スクリーンスペース反射 有効/無効");

    if (ssrOpen) {
        ImGui::Indent(12.0f);
        ImGui::SliderFloat("Intensity",        &pp->SsrIntensity,       0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Roughness Cutoff", &pp->SsrRoughnessCutoff, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Thickness",        &pp->SsrThickness,       0.05f, 2.0f, "%.2f");
        ImGui::SliderFloat("DDGI Fallback",    &pp->SsrDdgiFallback,    0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("画面外の反射をDDGIプローブで補完（DDGI有効時のみ）");
        ImGui::TextDisabled("ツヤのある面ほど反射（粗い面は無効）");
        ImGui::Unindent(12.0f);
    }

    // ── SSGI ───────────────────────────────────────────────────────────
    ImGui::SetNextItemAllowOverlap();
    bool ssgiOpen = ImGui::CollapsingHeader("SSGI", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    ImGui::Checkbox("##ssgi_en", &pp->SsgiEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("スクリーンスペースGI（1バウンス間接拡散）有効/無効");

    if (ssgiOpen) {
        ImGui::Indent(12.0f);
        ImGui::BeginDisabled(!pp->SsgiEnabled);
        ImGui::SliderFloat("Intensity##ssgi", &pp->SsgiIntensity, 0.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Radius##ssgi",    &pp->SsgiRadius,    0.5f, 20.0f, "%.2f");
        ImGui::SliderFloat("Thickness##ssgi", &pp->SsgiThickness, 0.05f, 2.0f, "%.2f");
        ImGui::SliderInt("Ray Count",  &pp->SsgiRayCount,  1, 16);
        ImGui::SliderInt("Step Count", &pp->SsgiStepCount, 2, 32);
        ImGui::Separator();
        ImGui::Checkbox("Denoise", &pp->SsgiDenoise);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("時間蓄積（再投影）＋エッジ保持ブラー。OFFで生のノイズ");
        ImGui::BeginDisabled(!pp->SsgiDenoise);
        ImGui::SliderFloat("History Blend", &pp->SsgiHistoryBlend, 0.0f, 0.98f, "%.2f");
        ImGui::SliderInt("Blur Radius",     &pp->SsgiBlurRadius,   0, 4);
        ImGui::EndDisabled();
        ImGui::TextDisabled("時間蓄積でザラつきを除去。動くと一時的に荒れて収束");
        ImGui::EndDisabled();
        ImGui::Unindent(12.0f);
    }

    // ── DDGI ───────────────────────────────────────────────────────────
    ImGui::SetNextItemAllowOverlap();
    bool ddgiOpen = ImGui::CollapsingHeader("DDGI", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    ImGui::Checkbox("##ddgi_en", &pp->DdgiEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("プローブボリュームGI（画面外の間接光）有効/無効");

    if (ddgiOpen) {
        ImGui::Indent(12.0f);
        ImGui::BeginDisabled(!pp->DdgiEnabled);
        ImGui::SliderFloat("Intensity##ddgi", &pp->DdgiIntensity, 0.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Feedback##ddgi", &pp->DdgiFeedback, 0.0f, 0.95f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("マルチバウンス: プローブ間接光の再注入率 (0=単バウンス)");
        ImGui::EndDisabled();
        ImGui::TextDisabled("画面外GIをカバー（SSGIは画面内）。Feedback>0でマルチバウンス");
        ImGui::Unindent(12.0f);
    }

    // ── Height Fog ─────────────────────────────────────────────────────
    ImGui::SetNextItemAllowOverlap();
    bool fogOpen = ImGui::CollapsingHeader("Height Fog", ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
    ImGui::Checkbox("##fog_en", &pp->FogEnabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("指数ハイトフォグ＋太陽インスキャッタ 有効/無効");

    if (fogOpen) {
        ImGui::Indent(12.0f);
        ImGui::BeginDisabled(!pp->FogEnabled);
        ImGui::DragFloat("Density",        &pp->FogDensity,       0.001f, 0.0f, 1.0f, "%.3f");
        ImGui::DragFloat("Height Falloff", &pp->FogHeightFalloff, 0.01f,  0.0f, 5.0f, "%.2f");
        ImGui::DragFloat("Fog Height",     &pp->FogHeight,        0.1f, -100.0f, 100.0f, "%.1f");
        ImGui::SliderFloat("Max Opacity",  &pp->FogMaxOpacity,    0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Inscatter Exp",  &pp->FogInscatterExp,  0.2f, 1.0f, 64.0f, "%.1f");
        ImGui::ColorEdit3("Fog Color",       pp->FogColor);
        ImGui::ColorEdit3("Inscatter Color", pp->FogInscatterColor);
        ImGui::TextDisabled("太陽方向を向くとフォグが発光");

        ImGui::Spacing();
        ImGui::SeparatorText("Volumetric (God Rays)");
        ImGui::SliderInt("Steps",       &pp->GodRaySteps,     0, 128);
        ImGui::DragFloat("Intensity##gr", &pp->GodRayIntensity, 0.01f, 0.0f, 5.0f, "%.2f");
        ImGui::DragFloat("Max Dist##gr",  &pp->GodRayMaxDist,   0.5f,  1.0f, 500.0f, "%.0f");
        ImGui::SliderFloat("Scattering G", &pp->GodRayG,        -0.9f, 0.9f, "%.2f");
        ImGui::TextDisabled("Steps=0で無効。物陰から光芒（太陽CSM）");
        ImGui::EndDisabled();
        ImGui::Unindent(12.0f);
    }

    ImGui::End();
}

} // namespace Fujin
