#include "Common.h"
#include "utils.hpp"
#include "math.hpp"
#include "shaders.hpp"
#include "geometries.hpp"
#include "SPIRV/GlslangToSpv.h"
#include "OGLCompilersDLL/InitializeDll.h"
#include "GraphicsObjects.h"

#if _DEBUG
#pragma comment(lib, "glslangd.lib")
#pragma comment(lib, "glslang-default-resource-limitsd.lib")
#pragma comment(lib, "HLSLd.lib")
#pragma comment(lib, "SPIRVd.lib")
#pragma comment(lib, "SPVRemapperd.lib")
#pragma comment(lib, "OGLCompilerd.lib")
#pragma comment(lib, "OSDependentd.lib")
#else
#pragma comment(lib, "glslang.lib")
#pragma comment(lib, "glslang-default-resource-limits.lib")
#pragma comment(lib, "HLSL.lib")
#pragma comment(lib, "SPIRV.lib")
#pragma comment(lib, "SPVRemapper.lib")
#pragma comment(lib, "OGLCompiler.lib")
#pragma comment(lib, "OSDependent.lib")
#endif

#pragma comment(lib, "vulkan-1.lib")

glm::mat4x4 createModelViewProjectionClipMatrix(float rot, vk::Extent2D const& extent)
{
    float fov = glm::radians(45.0f);
    if (extent.width > extent.height)
    {
        fov *= static_cast<float>(extent.height) / static_cast<float>(extent.width);
    }

    glm::mat4x4 model = glm::rotate(glm::mat4x4(1.0f), glm::radians(rot), glm::vec3(0, 1, 0));
    glm::mat4x4 view = glm::lookAt(glm::vec3(-5.0f, 3.0f, -10.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    glm::mat4x4 projection = glm::perspective(fov, 1.0f, 0.1f, 100.0f);
    glm::mat4x4 clip = glm::mat4x4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f);   // vulkan clip space has inverted y and half z !
    return clip * projection * view * model;
}

int main(int argc, char** argv)
{
    glslang::InitializeProcess();

    const char* appName = "Ray GPU";

    //vk::UniqueInstance instance = vk::su::createInstance(appName, appName, {}, vk::su::getInstanceExtensions(), VK_API_VERSION_1_0);

    //vk::PhysicalDevice physicalDevice = instance->enumeratePhysicalDevices().front();

    //vk::su::SurfaceData surfaceData(instance, appName, appName, vk::Extent2D(500, 500));

    //std::pair<uint32_t, uint32_t> graphicsAndPresentQueueFamilyIndex = vk::su::findGraphicsAndPresentQueueFamilyIndex(physicalDevice, *surfaceData.surface);
    
    //vk::UniqueDevice device = vk::su::createDevice(physicalDevice, graphicsAndPresentQueueFamilyIndex.first, vk::su::getDeviceExtensions());

    vk::UniqueCommandPool commandPool = vk::su::createCommandPool(device, graphicsAndPresentQueueFamilyIndex.first);
    vk::UniqueCommandBuffer commandBuffer = std::move(device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(commandPool.get(), vk::CommandBufferLevel::ePrimary, 1)).front());

    //vk::Queue graphicsQueue = device->getQueue(graphicsAndPresentQueueFamilyIndex.first, 0);
    //vk::Queue presentQueue = device->getQueue(graphicsAndPresentQueueFamilyIndex.second, 0);

    //vk::su::SwapChainData swapChainData(physicalDevice, device, *surfaceData.surface, surfaceData.extent, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
    //                                    vk::UniqueSwapchainKHR(), graphicsAndPresentQueueFamilyIndex.first, graphicsAndPresentQueueFamilyIndex.second);

    vk::su::DepthBufferData depthBufferData(physicalDevice, device, vk::Format::eD16Unorm, surfaceData.extent);

    vk::su::BufferData uniformBufferData(physicalDevice, device, sizeof(glm::mat4x4), vk::BufferUsageFlagBits::eUniformBuffer);
    vk::su::copyToDevice(device, uniformBufferData.deviceMemory, vk::su::createModelViewProjectionClipMatrix(surfaceData.extent));

    vk::UniqueDescriptorSetLayout descriptorSetLayout = vk::su::createDescriptorSetLayout(device, { {vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex} });
    vk::UniquePipelineLayout pipelineLayout = device->createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(vk::PipelineLayoutCreateFlags(), 1, &descriptorSetLayout.get()));

    vk::UniqueRenderPass renderPass = vk::su::createRenderPass(device, vk::su::pickSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(surfaceData.surface.get())).format, depthBufferData.format);

    vk::UniqueShaderModule vertexShaderModule = vk::su::createShaderModule(device, vk::ShaderStageFlagBits::eVertex, vertexShaderText_PC_C);
    vk::UniqueShaderModule fragmentShaderModule = vk::su::createShaderModule(device, vk::ShaderStageFlagBits::eFragment, fragmentShaderText_C_C);

    std::vector<vk::UniqueFramebuffer> framebuffers = vk::su::createFramebuffers(device, renderPass, swapChainData.imageViews, depthBufferData.imageView, surfaceData.extent);

    vk::su::BufferData vertexBufferData(physicalDevice, device, sizeof(coloredCubeData), vk::BufferUsageFlagBits::eVertexBuffer);
    vk::su::copyToDevice(device, vertexBufferData.deviceMemory, coloredCubeData, sizeof(coloredCubeData) / sizeof(coloredCubeData[0]));

    vk::UniqueDescriptorPool descriptorPool = vk::su::createDescriptorPool(device, { {vk::DescriptorType::eUniformBuffer, 1} });
    vk::UniqueDescriptorSet descriptorSet = std::move(device->allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(*descriptorPool, 1, &*descriptorSetLayout)).front());

    vk::su::updateDescriptorSets(device, descriptorSet, { {vk::DescriptorType::eUniformBuffer, uniformBufferData.buffer, vk::UniqueBufferView()} }, {});

    vk::UniquePipelineCache pipelineCache = device->createPipelineCacheUnique(vk::PipelineCacheCreateInfo());
    vk::UniquePipeline graphicsPipeline = vk::su::createGraphicsPipeline(device, pipelineCache, std::make_pair(*vertexShaderModule, nullptr), std::make_pair(*fragmentShaderModule, nullptr),
                                                                         sizeof(coloredCubeData[0]), { { vk::Format::eR32G32B32A32Sfloat, 0 }, { vk::Format::eR32G32B32A32Sfloat, 16 } },
                                                                         vk::FrontFace::eClockwise, true, pipelineLayout, renderPass);
    /* VULKAN_KEY_START */

    

    float testAngle = 0;

    // Get the index of the next available swapchain image:
    vk::UniqueSemaphore imageAcquiredSemaphore = device->createSemaphoreUnique(vk::SemaphoreCreateInfo());
    
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    while (msg.message != WM_QUIT)
    {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        vk::ResultValue<uint32_t> currentBuffer = device->acquireNextImageKHR(swapChainData.swapChain.get(), vk::su::FenceTimeout, imageAcquiredSemaphore.get(), nullptr);
        assert(currentBuffer.result == vk::Result::eSuccess);
        assert(currentBuffer.value < framebuffers.size());

        //////////////////////////////////////////////////////////////////////////

        testAngle += 0.01;

        vk::su::copyToDevice(device, uniformBufferData.deviceMemory, createModelViewProjectionClipMatrix(testAngle, surfaceData.extent));

        commandBuffer->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlags()));

        vk::ClearValue clearValues[2];
        clearValues[0].color = vk::ClearColorValue(std::array<float, 4>({ 0.2f, 0.2f, 0.2f, 0.2f }));
        clearValues[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
        vk::RenderPassBeginInfo renderPassBeginInfo(renderPass.get(), framebuffers[currentBuffer.value].get(), vk::Rect2D(vk::Offset2D(0, 0), surfaceData.extent), 2, clearValues);
        commandBuffer->beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline.get());
        commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout.get(), 0, descriptorSet.get(), nullptr);

        commandBuffer->bindVertexBuffers(0, *vertexBufferData.buffer, { 0 });
        commandBuffer->setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(surfaceData.extent.width), static_cast<float>(surfaceData.extent.height), 0.0f, 1.0f));
        commandBuffer->setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), surfaceData.extent));

        commandBuffer->draw(12 * 3, 1, 0, 0);
        commandBuffer->endRenderPass();
        commandBuffer->end();

        //////////////////////////////////////////////////////////////////////////

        vk::UniqueFence drawFence = device->createFenceUnique(vk::FenceCreateInfo());

        vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        vk::SubmitInfo submitInfo(1, &imageAcquiredSemaphore.get(), &waitDestinationStageMask, 1, &commandBuffer.get());
        graphicsQueue.submit(submitInfo, drawFence.get());
        
        while (vk::Result::eTimeout == device->waitForFences(drawFence.get(), VK_TRUE, vk::su::FenceTimeout))
            ;

        presentQueue.presentKHR(vk::PresentInfoKHR(0, nullptr, 1, &swapChainData.swapChain.get(), &currentBuffer.value));
    }
    //Sleep(1000);

    /* VULKAN_KEY_END */

    device->waitIdle();

    DestroyWindow(surfaceData.window);
    glslang::FinalizeProcess();

    return 0;
}