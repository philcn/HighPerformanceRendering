#pragma once

#include "Falcor.h"

using namespace Falcor;

class HighPerformanceRendering : public Renderer
{
public:
    void onLoad(SampleCallbacks* sample, RenderContext* renderContext) override;
    void onFrameRender(SampleCallbacks* sample, RenderContext* renderContext, const Fbo::SharedPtr& targetFbo) override;
    void onShutdown(SampleCallbacks* sample) override;
    void onResizeSwapChain(SampleCallbacks* sample, uint32_t width, uint32_t height) override;
    bool onKeyEvent(SampleCallbacks* sample, const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(SampleCallbacks* sample, const MouseEvent& mouseEvent) override;
    void onDataReload(SampleCallbacks* sample) override;
    void onGuiRender(SampleCallbacks* sample, Gui* gui) override;

private:
    void SetupScene();
    void SetupRendering(uint32_t width, uint32_t height);
    void RenderScene(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo);
    void RenderSceneExplicit(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo);
    void RenderSceneBindlessConstants(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo);

    void DrawSingleMesh(
        RenderContext* renderContext,
        const GraphicsVars::SharedPtr& vars,
        const GraphicsState::SharedPtr& state,
        const Mesh::SharedPtr& mesh,
        const Scene::ModelInstance::SharedPtr& modelInstance,
        const Model::MeshInstance::SharedPtr& meshInstance,
        std::function<void(const GraphicsVars::SharedPtr&, const Model::MeshInstance::SharedPtr&, const Scene::ModelInstance::SharedPtr&)> setPerDrawData);

    void UpdateShaderBindingLocations(const GraphicsVars::SharedPtr& vars);
    void SetPerFrameData(const GraphicsVars::SharedPtr& vars, const Camera::SharedPtr& camera, const Scene::SharedPtr& scene);
    void SetPerMaterialData(const GraphicsVars::SharedPtr& vars, const Material::SharedPtr& material);

    void EnableBindlessConstants(bool enable);
    
    Scene::SharedPtr mScene;

    Camera::SharedPtr mCamera;
    FirstPersonCameraController mCamController;

    SceneRenderer::SharedPtr mSceneRenderer;

    GraphicsProgram::SharedPtr mForwardProgram;
    GraphicsVars::SharedPtr mForwardVars;
    GraphicsState::SharedPtr mForwardState;

    uint32_t mDrawCount;
    bool mReferenceMode;
    bool mBindlessConstantsMode;
};
