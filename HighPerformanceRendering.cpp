#include "HighPerformanceRendering.h"

#define REPEAT_NEXT_BLOCK for (int i = 0; i < 500; ++i)

namespace
{
    // Relative to working directory. Note: different between running from VS and standalone
    static const char* kDefaultScene = "../../Media/Arcade/Arcade.fscene";
    static const glm::vec4 kClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    static const glm::vec4 kSkyColor(0.2f, 0.6f, 0.9f, 1.0f);

    const char* kPerFrameCbName = "InternalPerFrameCB";
    static size_t sCameraDataOffset = ConstantBuffer::kInvalidOffset;
    static size_t sLightCountOffset = ConstantBuffer::kInvalidOffset;
    static size_t sLightArrayOffset = ConstantBuffer::kInvalidOffset;

    const char* kPerMeshCbName = "InternalPerMeshCB";
    static size_t sWorldMatOffset = ConstantBuffer::kInvalidOffset;
    static size_t sPrevWorldMatOffset = ConstantBuffer::kInvalidOffset;
    static size_t sWorldInvTransposeMatOffset = ConstantBuffer::kInvalidOffset;
    static size_t sDrawIDOffset = ConstantBuffer::kInvalidOffset;
    static size_t sMeshIdOffset = ConstantBuffer::kInvalidOffset;
    static size_t sWorldMatArraySize = 0;
}

void HighPerformanceRendering::onLoad(SampleCallbacks* sample, RenderContext* renderContext)
{
    uint32_t width = sample->getCurrentFbo()->getWidth();
    uint32_t height = sample->getCurrentFbo()->getHeight();

    mCamera = Camera::create();
    mCamera->setAspectRatio((float)width / (float)height);
    mCamController.attachCamera(mCamera);
    mCamController.setCameraSpeed(5.0f);

    SetupScene();
    SetupRendering(width, height);

    mDrawCount = 0;
    mReferenceMode = false;
    mBindlessConstantsMode = false;
}

void HighPerformanceRendering::SetupScene()
{
    mScene = Scene::loadFromFile(kDefaultScene, Model::LoadFlags::None, Scene::LoadFlags::None);

    // Set scene specific camera parameters
    float radius = mScene->getRadius();
    float nearZ = std::max(0.1f, radius / 750.0f);
    float farZ = radius * 20;
        
    mCamera->setPosition(glm::vec3(-0.8f, 0.5f, 0.8f) * radius);
    mCamera->setTarget(mScene->getCenter());
    mCamera->setDepthRange(nearZ, farZ);
}

void HighPerformanceRendering::SetupRendering(uint32_t width, uint32_t height)
{
    mSceneRenderer = SceneRenderer::create(mScene);

    mForwardProgram = GraphicsProgram::createFromFile("Forward.slang", "MainVS", "MainPS");
    mForwardVars = GraphicsVars::create(mForwardProgram->getReflector());
    mForwardState = GraphicsState::create();
    mForwardState->setProgram(mForwardProgram);
    mForwardState->setRasterizerState(RasterizerState::create(RasterizerState::Desc().setCullMode(RasterizerState::CullMode::None)));
}

void HighPerformanceRendering::onFrameRender(SampleCallbacks* sample, RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    mCamera->beginFrame();
    mCamController.update();
    mSceneRenderer->update(sample->getCurrentTime());

    renderContext->clearFbo(targetFbo.get(), kSkyColor, 1.0f, 0u, FboAttachmentType::All);
 
    if (mReferenceMode)
    {
        RenderScene(renderContext, targetFbo);
    }
    else
    {
        if (mBindlessConstantsMode)
        {
            RenderSceneBindlessConstants(renderContext, targetFbo);
        }
        else
        {
            RenderSceneExplicit(renderContext, targetFbo);
        }
    }
}

void HighPerformanceRendering::RenderScene(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    PROFILE("RenderScene");

    mForwardState->setFbo(targetFbo);
    renderContext->setGraphicsState(mForwardState);
    renderContext->setGraphicsVars(mForwardVars);

    REPEAT_NEXT_BLOCK
    mSceneRenderer->renderScene(renderContext, mCamera.get());
}

void HighPerformanceRendering::RenderSceneExplicit(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    PROFILE("RenderSceneExplicit");

    // Populate "InternalPerMeshCB" with mesh transforms
    auto setPerMeshInstanceData = [&](const GraphicsVars::SharedPtr& vars, const Model::MeshInstance::SharedPtr& meshInstance, const Scene::ModelInstance::SharedPtr& modelInstance) 
    {
        const Mesh* pMesh = meshInstance->getObject().get();

        assert(!pMesh->hasBones());
        glm::mat4 worldMat = modelInstance->getTransformMatrix() * meshInstance->getTransformMatrix();
        glm::mat4 prevWorldMat = modelInstance->getPrevTransformMatrix() * meshInstance->getTransformMatrix();
        glm::mat3x4 worldInvTransposeMat = transpose(inverse(glm::mat3(worldMat)));

        const int drawInstanceID = 0; // No instancing

        ConstantBuffer* pCB = vars->getConstantBuffer(kPerMeshCbName).get();
        pCB->setBlob(&worldMat, sWorldMatOffset + drawInstanceID * sizeof(glm::mat4), sizeof(glm::mat4));
        pCB->setBlob(&worldInvTransposeMat, sWorldInvTransposeMatOffset + drawInstanceID * sizeof(glm::mat3x4), sizeof(glm::mat3x4));
        pCB->setBlob(&prevWorldMat, sPrevWorldMatOffset + drawInstanceID * sizeof(glm::mat4), sizeof(glm::mat4));

        pCB->setVariable(sMeshIdOffset, pMesh->getId());
    };

    mDrawCount = 0;
    mForwardState->setFbo(targetFbo);

    UpdateShaderBindingLocations(mForwardVars);
    SetPerFrameData(mForwardVars, mCamera, mScene);

    REPEAT_NEXT_BLOCK
    for (uint32_t modelID = 0; modelID < mScene->getModelCount(); ++modelID)
    {
        const auto& model = mScene->getModel(modelID);
        for (uint32_t modelInstanceID = 0; modelInstanceID < mScene->getModelInstanceCount(modelID); ++modelInstanceID)
        {
            const auto& modelInstance = mScene->getModelInstance(modelID, modelInstanceID);
            for (uint32_t meshID = 0; meshID < model->getMeshCount(); ++meshID)
            {
                const auto& mesh = model->getMesh(meshID);
                for (uint32_t meshInstanceID = 0; meshInstanceID < model->getMeshInstanceCount(meshID); ++meshInstanceID)
                {
                    const auto& meshInstance = model->getMeshInstance(meshID, meshInstanceID);
                    DrawSingleMesh(renderContext, mForwardVars, mForwardState, mesh, modelInstance, meshInstance, setPerMeshInstanceData);
                }
            }
        }
    }
}

void HighPerformanceRendering::RenderSceneBindlessConstants(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    PROFILE("BindlessConstants");

    struct DrawList
    {
        struct DrawConstants
        {
            glm::mat4 worldMat;
            glm::mat4 prevWorldMat;
            glm::mat3x4 worldInvTransposeMat;
            uint32_t drawID;
            uint32_t meshID;
            uint32_t pad[2]; // Note the manual padding to ensure 16 byte alignment
        };

        std::vector<Mesh::SharedPtr> meshes;
        std::vector<DrawConstants> constants;
        StructuredBuffer::SharedPtr drawConstantsBuffer;
        uint32_t numDrawItems = 0;
    };

    auto prepareDrawList = [&]() -> DrawList
    {
        DrawList drawList;

        REPEAT_NEXT_BLOCK
        for (uint32_t modelID = 0; modelID < mScene->getModelCount(); ++modelID)
        {
            const auto& model = mScene->getModel(modelID);
            for (uint32_t modelInstanceID = 0; modelInstanceID < mScene->getModelInstanceCount(modelID); ++modelInstanceID)
            {
                const auto& modelInstance = mScene->getModelInstance(modelID, modelInstanceID);
                for (uint32_t meshID = 0; meshID < model->getMeshCount(); ++meshID)
                {
                    const auto& mesh = model->getMesh(meshID);
                    for (uint32_t meshInstanceID = 0; meshInstanceID < model->getMeshInstanceCount(meshID); ++meshInstanceID)
                    {
                        const auto& meshInstance = model->getMeshInstance(meshID, meshInstanceID);

                        assert(!mesh->hasBones()); // Skinning requires different handling of transforms

                        DrawList::DrawConstants drawConstants;
                        drawConstants.worldMat = modelInstance->getTransformMatrix() * meshInstance->getTransformMatrix();
                        drawConstants.prevWorldMat = modelInstance->getPrevTransformMatrix() * meshInstance->getTransformMatrix();
                        drawConstants.worldInvTransposeMat = transpose(inverse(glm::mat3(drawConstants.worldMat)));
                        drawConstants.meshID = mesh->getId();
                        drawConstants.drawID = drawList.numDrawItems++;

                        drawList.meshes.push_back(mesh);
                        drawList.constants.push_back(drawConstants);
                    }
                }
            }
        }

        if (drawList.constants.size() > 0)
        {
            drawList.drawConstantsBuffer = StructuredBuffer::create(mForwardProgram, "gDrawConstants", drawList.constants.size());
            drawList.drawConstantsBuffer->setBlob(drawList.constants.data(), 0, sizeof(drawList.constants[0]) * drawList.constants.size());
        }

        return drawList;
    };

    // One-time preparation
    static DrawList drawList = prepareDrawList();
    std::atexit([] { drawList.drawConstantsBuffer = nullptr; }); // Destruct before Vulkan context is destroyed

    // Per frame draw operation
    mDrawCount = 0;
    mForwardState->setFbo(targetFbo);

    UpdateShaderBindingLocations(mForwardVars);
    SetPerFrameData(mForwardVars, mCamera, mScene);

    mForwardVars->setStructuredBuffer("gDrawConstants", drawList.drawConstantsBuffer);

    for (const auto& mesh : drawList.meshes)
    {
        DrawSingleMesh(renderContext, mForwardVars, mForwardState, mesh, nullptr, nullptr, nullptr);
    }
}

void HighPerformanceRendering::DrawSingleMesh(
    RenderContext* renderContext,
    const GraphicsVars::SharedPtr& vars,
    const GraphicsState::SharedPtr& state,
    const Mesh::SharedPtr& mesh,
    const Scene::ModelInstance::SharedPtr& modelInstance,
    const Model::MeshInstance::SharedPtr& meshInstance,
    std::function<void(const GraphicsVars::SharedPtr&, const Model::MeshInstance::SharedPtr&, const Scene::ModelInstance::SharedPtr&)> setPerDrawData)
{
    SetPerMaterialData(vars, mesh->getMaterial());

    if (setPerDrawData)
    {
        setPerDrawData(vars, meshInstance, modelInstance);
    }

    state->setVao(mesh->getVao());

#ifdef FALCOR_VK
    struct alignas(16) { uint32_t drawID; } push = { mDrawCount };
    renderContext->pushConstants(vars, sizeof(push), &push);
#else
    #error This application needs push constants to work
#endif

    renderContext->setGraphicsState(state);
    renderContext->setGraphicsVars(vars);
    renderContext->drawIndexed(mesh->getIndexCount(), 0, 0);

    mDrawCount++;
}

void HighPerformanceRendering::UpdateShaderBindingLocations(const GraphicsVars::SharedPtr& vars)
{
    const ParameterBlockReflection* pBlock = vars->getReflection()->getDefaultParameterBlock().get();

    if (sWorldMatOffset == ConstantBuffer::kInvalidOffset)
    {
        const ReflectionVar* pVar = pBlock->getResource(kPerMeshCbName).get();
        if (pVar != nullptr)
        {
            const ReflectionType* pType = pVar->getType().get();
            sWorldMatArraySize = pType->findMember("gWorldMat")->getType()->getTotalArraySize();
            sWorldMatOffset = pType->findMember("gWorldMat[0]")->getOffset();
            sPrevWorldMatOffset = pType->findMember("gPrevWorldMat[0]")->getOffset();
            sWorldInvTransposeMatOffset = pType->findMember("gWorldInvTransposeMat[0]")->getOffset();
            sDrawIDOffset = pType->findMember("gDrawId[0]")->getOffset();
            sMeshIdOffset = pType->findMember("gMeshId")->getOffset();
        }
    }

    if (sCameraDataOffset == ConstantBuffer::kInvalidOffset)
    {
        const ReflectionVar* pVar = pBlock->getResource(kPerFrameCbName).get();
        if (pVar != nullptr)
        {
            const ReflectionType* pType = pVar->getType().get();
            sCameraDataOffset = pType->findMember("gCamera.viewMat")->getOffset();
            sLightCountOffset = pType->findMember("gLightsCount")->getOffset();
            sLightArrayOffset = pType->findMember("gLights")->getOffset();
        }
    }
}

void HighPerformanceRendering::SetPerFrameData(const GraphicsVars::SharedPtr& vars, const Camera::SharedPtr& camera, const Scene::SharedPtr& scene)
{
    // Populate "InternalPerFrameCB" with camera and lights
    ConstantBuffer* pCB = vars->getConstantBuffer(kPerFrameCbName).get();
    assert(pCB != nullptr);

    if (camera)
    {
        camera->setIntoConstantBuffer(pCB, sCameraDataOffset);
    }

    if (sLightArrayOffset != ConstantBuffer::kInvalidOffset)
    {
        for (uint32_t i = 0; i < scene->getLightCount(); i++)
        {
            scene->getLight(i)->setIntoProgramVars(vars.get(), pCB, sLightArrayOffset + ((size_t)i * Light::getShaderStructSize()));
        }
    }

    if (sLightCountOffset != ConstantBuffer::kInvalidOffset)
    {
        pCB->setVariable(sLightCountOffset, scene->getLightCount());
    }
}

void HighPerformanceRendering::SetPerMaterialData(const GraphicsVars::SharedPtr& vars, const Material::SharedPtr& material)
{
    vars->setParameterBlock("gMaterial", material->getParameterBlock());
}

void HighPerformanceRendering::EnableBindlessConstants(bool enable)
{
    if (enable)
    {
        mForwardProgram->addDefine("BINDLESS_CONSTANTS");
    }
    else
    {
        mForwardProgram->removeDefine("BINDLESS_CONSTANTS");
    }

    if (mBindlessConstantsMode != enable)
    {
        mForwardVars = GraphicsVars::create(mForwardProgram->getReflector());
    }

    mBindlessConstantsMode = enable;
}

void HighPerformanceRendering::onGuiRender(SampleCallbacks* sample, Gui* gui)
{
}

bool HighPerformanceRendering::onKeyEvent(SampleCallbacks* sample, const KeyboardEvent& keyEvent)
{
    if (mCamController.onKeyEvent(keyEvent)) return true;
    if (keyEvent.type == KeyboardEvent::Type::KeyReleased)
    {
        if (keyEvent.key == KeyboardEvent::Key::R)
        {
            mReferenceMode ^= true;
            if (mReferenceMode)
            {
                EnableBindlessConstants(false);
            }
            return true;
        }
        if (keyEvent.key == KeyboardEvent::Key::B)
        {
            mReferenceMode = false;
            EnableBindlessConstants(!mBindlessConstantsMode);
            return true;
        }
    }
    return false;
}

bool HighPerformanceRendering::onMouseEvent(SampleCallbacks* sample, const MouseEvent& mouseEvent)
{
    if (mCamController.onMouseEvent(mouseEvent)) return true;
    return false;
}

void HighPerformanceRendering::onDataReload(SampleCallbacks* sample)
{
}

void HighPerformanceRendering::onResizeSwapChain(SampleCallbacks* sample, uint32_t width, uint32_t height)
{
}

void HighPerformanceRendering::onShutdown(SampleCallbacks* sample)
{
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    Logger::setVerbosity(Logger::Level::Warning);

    HighPerformanceRendering::UniquePtr pRenderer = std::make_unique<HighPerformanceRendering>();
    SampleConfig config;
    config.windowDesc.title = "HighPerformanceRendering";
    config.windowDesc.resizableWindow = true;
    Sample::run(config, pRenderer);
    return 0;
}
