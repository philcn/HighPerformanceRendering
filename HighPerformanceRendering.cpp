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

    mDrawCount = 0;
    mRenderMode = RenderMode::BindlessMultiDraw;

    SetupScene();
    SetupRendering(width, height);

    ConfigureRenderMode();
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
 
    if (mRenderMode == RenderMode::Stock)
    {
        RenderScene(renderContext, targetFbo);
    }
    else if (mRenderMode == RenderMode::Explicit)
    {
        RenderSceneExplicit(renderContext, targetFbo);
    }
    else if (mRenderMode == RenderMode::BindlessConstants)
    {
        RenderSceneBindlessConstants(renderContext, targetFbo);
    }
    else if (mRenderMode == RenderMode::BindlessMultiDraw)
    {
        RenderSceneBindlessMultiDraw(renderContext, targetFbo);
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

        uint32_t numDrawItems = 0;
        std::vector<Mesh::SharedPtr> meshes;
        std::vector<DrawConstants> constants; // CPU copy of constants for debug purpose only
        StructuredBuffer::SharedPtr drawConstantsBuffer;
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

    std::atexit([]
    { 
        // Destruct before Vulkan context is destroyed
        drawList.meshes.clear();
        drawList.drawConstantsBuffer = nullptr;
    }); 

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

void HighPerformanceRendering::RenderSceneBindlessMultiDraw(RenderContext* renderContext, const Fbo::SharedPtr& targetFbo)
{
    PROFILE("BindlessMultiDraw");

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

        uint32_t numDrawItems = 0;

        std::vector<DrawConstants> constants; // CPU copy of constants for debug purpose only
        StructuredBuffer::SharedPtr drawConstantsBuffer;

        Vao::SharedPtr vao;
        Buffer::SharedPtr indirectArgBuffer;
        Material::SharedPtr proxyMaterial; // The material to bind for now (until bindless material is implemented)
    };

    auto prepareDrawList = [&]() -> DrawList
    {
        DrawList drawList;

        const auto& protoVao = mScene->getModel(0)->getMesh(0)->getVao();
        const uint32_t vertexStreamCount = protoVao->getVertexBuffersCount();
        auto indexFormat = protoVao->getIndexBufferFormat();
        assert(indexFormat == ResourceFormat::R32Uint);

        // Combined vertex and index data
        std::vector<std::vector<uint8_t>> vertexStreams(vertexStreamCount);
        std::vector<uint8_t> indices;

        // Offsets for building indirect args
        std::vector<uint32_t> vertexOffsets;
        std::vector<uint32_t> indexOffsets;
        std::vector<uint32_t> indexCounts;

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

                        drawList.constants.push_back(drawConstants);

                        // Append vertices and indices into combined buffers
                        const auto& vao = mesh->getVao();
                        for (uint32_t i = 0; i < vertexStreamCount; ++i)
                        {
                            const auto& vb = vao->getVertexBuffer(i);
                            const void* vbData = vb->map(Buffer::MapType::Read);
                            const uint32_t vbSize = (uint32_t)vb->getSize();
                            const uint32_t vertexStride = protoVao->getVertexLayout()->getBufferLayout(i)->getStride();
                            const uint32_t vertexCount = vbSize / vertexStride;

                            auto& vertices = vertexStreams[i];

                            const size_t currentVBOffset = vertices.size();
                            vertices.resize(currentVBOffset + vertexCount * vertexStride);
                            memcpy(vertices.data() + currentVBOffset, vbData, vbSize);

                            if (i == 0)
                            {
                                vertexOffsets.push_back((uint32_t)currentVBOffset / vertexStride);
                            }
                        }

                        const void* ibData = vao->getIndexBuffer()->map(Buffer::MapType::Read);
                        const uint32_t ibSize = (uint32_t)vao->getIndexBuffer()->getSize();
                        const uint32_t indexCount = ibSize / sizeof(uint32_t);

                        const size_t currentIBOffset = indices.size();
                        indices.resize(currentIBOffset + indexCount * sizeof(uint32_t));
                        memcpy(indices.data() + currentIBOffset, ibData, ibSize);

                        indexOffsets.push_back((uint32_t)currentIBOffset / sizeof(uint32_t));
                        indexCounts.push_back(indexCount);
                    }
                }
            }
        }

        if (drawList.constants.size() > 0)
        {
            // Build bindless draw constants buffer
            drawList.drawConstantsBuffer = StructuredBuffer::create(mForwardProgram, "gDrawConstants", drawList.constants.size());
            drawList.drawConstantsBuffer->setBlob(drawList.constants.data(), 0, sizeof(drawList.constants[0]) * drawList.constants.size());

            // Build combined VAO
            Vao::BufferVec vbs;
            for (const auto& vertices : vertexStreams)
            {
                auto vb = Buffer::create(vertices.size(), Buffer::BindFlags::Vertex, Buffer::CpuAccess::None, vertices.data());;
                vbs.push_back(vb);
            }
            auto ib = Buffer::create(indices.size(), Buffer::BindFlags::Index, Buffer::CpuAccess::None, indices.data());;
            drawList.vao = Vao::create(protoVao->getPrimitiveTopology(), protoVao->getVertexLayout(), vbs, ib, indexFormat);

            // Build indirect arguments
            std::vector<DrawIndexedArguments> drawArgs;
            for (uint32_t i = 0; i < drawList.numDrawItems; ++i)
            {
                DrawIndexedArguments args = {};
                args.indexCountPerInstance = indexCounts[i];
                args.startIndexLocation = indexOffsets[i];
                args.baseVertexLocation = vertexOffsets[i];
                args.instanceCount = 1;
                args.startInstanceLocation = 0;
                     
                drawArgs.emplace_back(args);
            }

            drawList.indirectArgBuffer = Buffer::create(drawArgs.size() * sizeof(DrawIndexedArguments), Resource::BindFlags::IndirectArg, Buffer::CpuAccess::Write, drawArgs.data());
            drawList.proxyMaterial = mScene->getModel(0)->getMesh(0)->getMaterial();
        }

        return drawList;
    };

    // One-time preparation
    static DrawList drawList = prepareDrawList();

    std::atexit([]
    { 
        // Destruct before Vulkan context is destroyed
        drawList.drawConstantsBuffer = nullptr;
        drawList.vao = nullptr;
        drawList.indirectArgBuffer = nullptr;
        drawList.proxyMaterial = nullptr;
    }); 

    // Per frame draw operation
    mForwardState->setFbo(targetFbo);

    UpdateShaderBindingLocations(mForwardVars);
    SetPerFrameData(mForwardVars, mCamera, mScene);

    mForwardVars->setStructuredBuffer("gDrawConstants", drawList.drawConstantsBuffer);

    SetPerMaterialData(mForwardVars, drawList.proxyMaterial);

    mForwardState->setVao(drawList.vao);

    renderContext->setGraphicsState(mForwardState);
    renderContext->setGraphicsVars(mForwardVars);
    renderContext->multiDrawIndexedIndirect(drawList.indirectArgBuffer.get(), 0, drawList.numDrawItems, sizeof(DrawIndexedArguments));
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

void HighPerformanceRendering::ConfigureRenderMode()
{
    if (mRenderMode == RenderMode::Stock || mRenderMode == RenderMode::Explicit)
    {
        mForwardProgram->setDefines({});
    }
    else if (mRenderMode == RenderMode::BindlessConstants)
    {
        mForwardProgram->addDefine("BINDLESS_CONSTANTS");
    }
    else if (mRenderMode == RenderMode::BindlessMultiDraw)
    {
        mForwardProgram->addDefine("MULTI_DRAW");
    }

    mForwardVars = GraphicsVars::create(mForwardProgram->getReflector());
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
            mRenderMode = RenderMode::Stock;
            ConfigureRenderMode();
            return true;
        }
        if (keyEvent.key == KeyboardEvent::Key::X)
        {
            mRenderMode = RenderMode::Explicit;
            ConfigureRenderMode();
            return true;
        }
        if (keyEvent.key == KeyboardEvent::Key::B)
        {
            mRenderMode = RenderMode::BindlessConstants;
            ConfigureRenderMode();
            return true;
        }
        if (keyEvent.key == KeyboardEvent::Key::M)
        {
            mRenderMode = RenderMode::BindlessMultiDraw;
            ConfigureRenderMode();
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
