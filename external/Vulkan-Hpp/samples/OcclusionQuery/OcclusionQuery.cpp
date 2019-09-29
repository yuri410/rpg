// Copyright(c) 2019, NVIDIA CORPORATION. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// VulkanHpp Samples : OcclusionQuery
//                     Use occlusion query to determine if drawing renders any samples.

#include "../utils/geometries.hpp"
#include "../utils/math.hpp"
#include "../utils/shaders.hpp"
#include "../utils/utils.hpp"
#include "vulkan/vulkan.hpp"
#include "SPIRV/GlslangToSpv.h"
#include <iostream>

static char const* AppName = "OcclusionQuery";
static char const* EngineName = "Vulkan.hpp";

int main(int /*argc*/, char ** /*argv*/)
{
  try
  {
    vk::UniqueInstance instance = vk::su::createInstance(AppName, EngineName, {}, vk::su::getInstanceExtensions());
#if !defined(NDEBUG)
    vk::UniqueDebugUtilsMessengerEXT debugUtilsMessenger = vk::su::createDebugUtilsMessenger(instance);
#endif

    vk::PhysicalDevice physicalDevice = instance->enumeratePhysicalDevices().front();

    vk::su::SurfaceData surfaceData(instance, AppName, AppName, vk::Extent2D(500, 500));

    std::pair<uint32_t, uint32_t> graphicsAndPresentQueueFamilyIndex = vk::su::findGraphicsAndPresentQueueFamilyIndex(physicalDevice, *surfaceData.surface);
    vk::UniqueDevice device = vk::su::createDevice(physicalDevice, graphicsAndPresentQueueFamilyIndex.first, vk::su::getDeviceExtensions());

    vk::UniqueCommandPool commandPool = vk::su::createCommandPool(device, graphicsAndPresentQueueFamilyIndex.first);
    vk::UniqueCommandBuffer commandBuffer = std::move(device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(commandPool.get(), vk::CommandBufferLevel::ePrimary, 1)).front());

    vk::Queue graphicsQueue = device->getQueue(graphicsAndPresentQueueFamilyIndex.first, 0);
    vk::Queue presentQueue = device->getQueue(graphicsAndPresentQueueFamilyIndex.second, 0);

    vk::su::SwapChainData swapChainData(physicalDevice, device, *surfaceData.surface, surfaceData.extent, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
                                        vk::UniqueSwapchainKHR(), graphicsAndPresentQueueFamilyIndex.first, graphicsAndPresentQueueFamilyIndex.second);

    vk::su::DepthBufferData depthBufferData(physicalDevice, device, vk::Format::eD16Unorm, surfaceData.extent);

    vk::su::BufferData uniformBufferData(physicalDevice, device, sizeof(glm::mat4x4), vk::BufferUsageFlagBits::eUniformBuffer);
    vk::su::copyToDevice(device, uniformBufferData.deviceMemory, vk::su::createModelViewProjectionClipMatrix(surfaceData.extent));

    vk::UniqueDescriptorSetLayout descriptorSetLayout = vk::su::createDescriptorSetLayout(device, { {vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex} });
    vk::UniquePipelineLayout pipelineLayout = device->createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo(vk::PipelineLayoutCreateFlags(), 1, &descriptorSetLayout.get()));

    vk::UniqueRenderPass renderPass = vk::su::createRenderPass(device, vk::su::pickSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(surfaceData.surface.get())).format, depthBufferData.format);

    glslang::InitializeProcess();
    vk::UniqueShaderModule vertexShaderModule = vk::su::createShaderModule(device, vk::ShaderStageFlagBits::eVertex, vertexShaderText_PC_C);
    vk::UniqueShaderModule fragmentShaderModule = vk::su::createShaderModule(device, vk::ShaderStageFlagBits::eFragment, fragmentShaderText_C_C);
    glslang::FinalizeProcess();

    std::vector<vk::UniqueFramebuffer> framebuffers = vk::su::createFramebuffers(device, renderPass, swapChainData.imageViews, depthBufferData.imageView, surfaceData.extent);

    vk::su::BufferData vertexBufferData(physicalDevice, device, sizeof(coloredCubeData), vk::BufferUsageFlagBits::eVertexBuffer);
    vk::su::copyToDevice(device, vertexBufferData.deviceMemory, coloredCubeData, sizeof(coloredCubeData) / sizeof(coloredCubeData[0]));

    vk::UniqueDescriptorPool descriptorPool = vk::su::createDescriptorPool(device, { {vk::DescriptorType::eUniformBuffer, 1} });
    vk::UniqueDescriptorSet descriptorSet = std::move(device->allocateDescriptorSetsUnique(vk::DescriptorSetAllocateInfo(*descriptorPool, 1, &*descriptorSetLayout)).front());

    vk::su::updateDescriptorSets(device, descriptorSet, {{vk::DescriptorType::eUniformBuffer, uniformBufferData.buffer, vk::UniqueBufferView()}}, {});

    vk::UniquePipelineCache pipelineCache = device->createPipelineCacheUnique(vk::PipelineCacheCreateInfo());
    vk::UniquePipeline graphicsPipeline = vk::su::createGraphicsPipeline(device, pipelineCache, std::make_pair(*vertexShaderModule, nullptr), std::make_pair(*fragmentShaderModule, nullptr),
                                                                         sizeof(coloredCubeData[0]), { { vk::Format::eR32G32B32A32Sfloat, 0 }, { vk::Format::eR32G32B32A32Sfloat, 16 } },
                                                                         vk::FrontFace::eClockwise, true, pipelineLayout, renderPass);

    /* VULKAN_KEY_START */

    vk::UniqueSemaphore imageAcquiredSemaphore = device->createSemaphoreUnique(vk::SemaphoreCreateInfo(vk::SemaphoreCreateFlags()));

    // Get the index of the next available swapchain image:
    vk::ResultValue<uint32_t> currentBuffer = device->acquireNextImageKHR(swapChainData.swapChain.get(), UINT64_MAX, imageAcquiredSemaphore.get(), nullptr);
    assert(currentBuffer.result == vk::Result::eSuccess);
    assert(currentBuffer.value < framebuffers.size());

    /* Allocate a uniform buffer that will take query results. */
    vk::UniqueBuffer queryResultBuffer = device->createBufferUnique(vk::BufferCreateInfo(vk::BufferCreateFlags(), 4 * sizeof(uint64_t), vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst));

    vk::MemoryRequirements memoryRequirements = device->getBufferMemoryRequirements(queryResultBuffer.get());
    uint32_t memoryTypeIndex = vk::su::findMemoryType(physicalDevice.getMemoryProperties(), memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    vk::UniqueDeviceMemory queryResultMemory = device->allocateMemoryUnique(vk::MemoryAllocateInfo(memoryRequirements.size, memoryTypeIndex));

    device->bindBufferMemory(queryResultBuffer.get(), queryResultMemory.get(), 0);

    vk::UniqueQueryPool queryPool = device->createQueryPoolUnique(vk::QueryPoolCreateInfo(vk::QueryPoolCreateFlags(), vk::QueryType::eOcclusion, 2, vk::QueryPipelineStatisticFlags()));

    commandBuffer->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlags()));
    commandBuffer->resetQueryPool(queryPool.get(), 0, 2);

    vk::ClearValue clearValues[2];
    clearValues[0].color = vk::ClearColorValue(std::array<float, 4>({ 0.2f, 0.2f, 0.2f, 0.2f }));
    clearValues[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
    commandBuffer->beginRenderPass(vk::RenderPassBeginInfo(renderPass.get(), framebuffers[currentBuffer.value].get(), vk::Rect2D(vk::Offset2D(), surfaceData.extent), 2, clearValues), vk::SubpassContents::eInline);

    commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline.get());
    commandBuffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout.get(), 0, descriptorSet.get(), {});

    commandBuffer->bindVertexBuffers(0, *vertexBufferData.buffer, {0});
    commandBuffer->setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(surfaceData.extent.width), static_cast<float>(surfaceData.extent.height), 0.0f, 1.0f));
    commandBuffer->setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), surfaceData.extent));

    commandBuffer->beginQuery(queryPool.get(), 0, vk::QueryControlFlags());
    commandBuffer->endQuery(queryPool.get(), 0);

    commandBuffer->beginQuery(queryPool.get(), 1, vk::QueryControlFlags());
    commandBuffer->draw(12 * 3, 1, 0, 0);
    commandBuffer->endRenderPass();
    commandBuffer->endQuery(queryPool.get(), 1);

    commandBuffer->copyQueryPoolResults(queryPool.get(), 0, 2, queryResultBuffer.get(), 0, sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
    commandBuffer->end();

    vk::UniqueFence drawFence = device->createFenceUnique(vk::FenceCreateInfo());

    vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    vk::SubmitInfo submitInfo(1, &imageAcquiredSemaphore.get(), &waitDestinationStageMask, 1, &commandBuffer.get());
    graphicsQueue.submit(submitInfo, drawFence.get());

    graphicsQueue.waitIdle();

    uint64_t samplesPassed[2];
    device->getQueryPoolResults(queryPool.get(), 0, 2, vk::ArrayProxy<uint64_t>(4, samplesPassed), sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);

    std::cout << "vkGetQueryPoolResults data\n";
    std::cout << "samples_passed[0] = " << samplesPassed[0] << "\n";
    std::cout << "samples_passed[1] = " << samplesPassed[1] << "\n";

    /* Read back query result from buffer */
    uint64_t *samplesPassedPtr = static_cast<uint64_t*>(device->mapMemory(queryResultMemory.get(), 0, memoryRequirements.size, vk::MemoryMapFlags()));

    std::cout << "vkCmdCopyQueryPoolResults data\n";
    std::cout << "samples_passed[0] = " << samplesPassedPtr[0] << "\n";
    std::cout << "samples_passed[1] = " << samplesPassedPtr[1] << "\n";

    device->unmapMemory(queryResultMemory.get());

    while (vk::Result::eTimeout == device->waitForFences(drawFence.get(), VK_TRUE, vk::su::FenceTimeout))
      ;

    presentQueue.presentKHR(vk::PresentInfoKHR(0, nullptr, 1, &swapChainData.swapChain.get(), &currentBuffer.value));
    Sleep(1000);

    /* VULKAN_KEY_END */

#if defined(VK_USE_PLATFORM_WIN32_KHR)
    DestroyWindow(surfaceData.window);
#else
#pragma error "unhandled platform"
#endif
  }
  catch (vk::SystemError err)
  {
    std::cout << "vk::SystemError: " << err.what() << std::endl;
    exit(-1);
  }
  catch (std::runtime_error err)
  {
    std::cout << "std::runtime_error: " << err.what() << std::endl;
    exit(-1);
  }
  catch (...)
  {
    std::cout << "unknown error\n";
    exit(-1);
  }
  return 0;
}
