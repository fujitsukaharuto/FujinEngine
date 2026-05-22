# 私の新しいエンジン(要望)
 - visualstudio2022、C++
 - unrealベース(ただ、ブループリントのような機能はいいや。C++でコードを書くようなのに慣れてしまったから)
 - GUI上でいろいろ設定ができるようにしたい
 - Niagaraのようなシステムも入れたい
 - ダブルバッファリングは行いたい
 - レンダリングパスの編集もしやすく（コードで毎回編集するのではなく、GUI上で）
 - コンポーネント思考？
 - シーンの保存などができるように
 - unrealのようなライティング
 - アクタとジオメトリ
 - ポストプロセスエフェクト
 - マテリアルの編集をしやすく
 -  DX12でimguiを使って作成していくつもり
 - フォルダ分けを意識しながら


# 新エンジン「Fujin Engine」設計計画
    
 ## 1. 目的とビジョン
 - **Unreal Engineベースの設計**: アクタ、コンポーネント、ジオメトリの概念を導入。
 - **モダンなDX12実装**: Render Graph（Frame Graph）を採用し、パスの編集を容易にする。
 - **拡張性とツール**: ImGuiを使用した強力なエディタ機能（マテリアル、レンダリングパス、Niagara風VFX）。
 - **C++重視**: 高いパフォーマンスと自由度を維持しつつ、使いやすいAPI。

 ## 2. アーキテクチャ構成

 ### Core (核となるシステム)
 - **Task System / Job System**: 非同期処理、マルチスレッドの最適化。
 - **Memory Management**: カスタムアロケータ、プーリング。
 - **Reflection / Serialization**: `nlohmann/json` を使用したシーン、アセットの保存。
 ### Graphics (DX12)
 - **RHI (Render Hardware Interface)**: DX12のラップ。
 - **Render Graph / Frame Graph**:
     - パス間の依存関係を自動解決。
     - バリア遷移の自動化。
     - GUI上でのパス追加・編集を可能にする設計。
 - **Shader Management**: HLSL 6.x+, 自動コンパイル、定数バッファの自動バインド。
 - **Lighting**: Deferred / Forward+ / Clustered Shading の検討。PBR (Physically Based Rendering)。

 ### Scene Management
 - **Actor-Component System**:
     - `Actor`: シーン内のエンティティ。
     - `Component`: 機能（Transform, Mesh, Light, etc.）。
 - **Scene Hierarchy**: 親子関係の管理。

 ### VFX System (Niagara風)
 - **Emitter-Particle 構成**:
     - Data-Driven なパーティクル制御。
     - Compute Shader による大量のパーティクル処理。
     - GUIでのモジュール編集。

 ### Editor (ImGui)
 - **Scene View**: ギズモ操作。
 - **Inspector**: コンポーネントのプロパティ編集。
 - **Content Browser**: アセット管理。
 - **Render Pass / Material Editor**: ノードベースまたはリストベースの編集画面。

 ## 3. 開発フェーズ (マイルストーン)

 ### フェーズ 1: 基盤構築
 1. Window生成、入力管理。
 2. DX12 初期化 (Device, Command Queue, SwapChain)。
 3. メモリ管理、タスクシステムの導入。

 ### フェーズ 2: レンダリングエンジン
 1. Render Graph のプロトタイプ。
 2. 基本的な描画 (Triangle -> Mesh)。
 3. PBR マテリアルとライティング (Directional Light, Point Light)。
 4. ポストプロセススタック。

 ### フェーズ 3: シーンとエディタ
 1. アクタ・コンポーネントシステムの実装。
 2. シーンのシリアライズ (保存・読み込み)。
 3. ImGui によるエディタ基盤 (Inspector, Content Browser)。

 ### フェーズ 4: 高度な機能
 1. Niagara風VFXシステム。
 2. マテリアルエディタ。
 3. シャドウマップ、IBL。

 ## 4. 検証項目
 - **パフォーマンス**: DX12の並列描画命令発行の効率。
 - **柔軟性**: 新しいレンダリングパスをGUIから追加し、正常に動作するか。
 - **安定性**: リソースのライフサイクル管理 (リリースのタイミングなど)。


 # 必要なImGUIや他の拡張機能があった場合
 - 必要な拡張機能を指定してくれれば私がGitHubなどから持って来て追加します