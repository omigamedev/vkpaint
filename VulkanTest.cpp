#include <iostream>
#include <array>
#include <vector>
#include <fstream>
#include <filesystem>
#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.hpp>

#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/euler_angles.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include "debug_message.h"

using cs = vk::ComponentSwizzle;

struct vertex_t
{
    glm::vec2 pos;
    glm::vec3 col;
    glm::vec2 tex;
    vertex_t() = default;
    constexpr vertex_t(glm::vec2 p, glm::vec3 c, glm::vec2 t) : pos(p), col(c), tex(t) {}
};

HWND create_window(int width, int height)
{
    WNDCLASSA wc{ 0 };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hInstance = GetModuleHandle(0);
    wc.lpszClassName = "MainVulkanWindow";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpfnWndProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_CLOSE)
            exit(0);
        return DefWindowProcA(hWnd, uMsg, wParam, lParam); 
    };
    if (!RegisterClassA(&wc))
        exit(1);
    RECT r = { 0, 0, width, height };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, false);
    return CreateWindowA(wc.lpszClassName, "Vulkan", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_SYSMENU, 0, 0,
        r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
}

int find_memory(const vk::PhysicalDevice& pd, const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags)
{
    vk::PhysicalDeviceMemoryProperties mp = pd.getMemoryProperties();
    for (size_t mem_i = 0; mem_i < mp.memoryTypeCount; mem_i++)
        if ((1 << mem_i) & req.memoryTypeBits && (mp.memoryTypes[mem_i].propertyFlags & flags) == flags)
            return mem_i;
    throw std::runtime_error("find_memory failed");
    return -1;
}

std::vector<uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs)
        throw std::runtime_error("read_file failed to open the file " + path.string());
    std::vector<uint8_t> buffer(ifs.tellg());
    ifs.seekg(0, std::ios::beg);
    ifs.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    return buffer;
}

vk::UniqueShaderModule load_shader(const vk::Device& dev, const std::filesystem::path& path)
{
    auto code = read_file(path);
    return dev.createShaderModuleUnique({ {}, code.size(), 
        reinterpret_cast<uint32_t*>(code.data()) });
}

auto init_pipeline(const vk::UniqueDevice& dev, const vk::UniqueShaderModule& vert, 
    const vk::UniqueShaderModule& frag, vk::Extent2D extent)
{
    vk::PipelineShaderStageCreateInfo pipeline_stages[] = {
        { {}, vk::ShaderStageFlagBits::eVertex, *vert, "main", nullptr },
        { {}, vk::ShaderStageFlagBits::eFragment, *frag, "main", nullptr }
    };
    auto input_binding = vk::VertexInputBindingDescription(0, sizeof(vertex_t), vk::VertexInputRate::eVertex);
    std::array<vk::VertexInputAttributeDescription, 3> input_attr = {
        vk::VertexInputAttributeDescription{ 0, 0, vk::Format::eR32G32Sfloat, offsetof(vertex_t, pos) },
        vk::VertexInputAttributeDescription{ 1, 0, vk::Format::eR32G32B32Sfloat, offsetof(vertex_t, col) },
        vk::VertexInputAttributeDescription{ 2, 0, vk::Format::eR32G32Sfloat, offsetof(vertex_t, tex) },
    };
    auto pipeline_vertex_input = vk::PipelineVertexInputStateCreateInfo({}, 1, 
        &input_binding, input_attr.size(), input_attr.data());
    auto pipeline_input_assembly = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleList, false);
    auto pipeline_vp = vk::Viewport(0, 0, extent.width, extent.height, 0, 1);
    auto pipeline_vpscissor = vk::Rect2D({ 0, 0 }, extent);
    auto pipeline_vpstate = vk::PipelineViewportStateCreateInfo({}, 1, &pipeline_vp, 1, &pipeline_vpscissor);
    auto pipeline_raster = vk::PipelineRasterizationStateCreateInfo({}, false, false, vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise, false, 0, 0, 0, 1.f);
    auto pipeline_ms = vk::PipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1, false, 1.f, nullptr, false, false);
    auto pipeline_blend_state = vk::PipelineColorBlendAttachmentState();
    pipeline_blend_state.setColorWriteMask(vk::ColorComponentFlagBits::eR |
        vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    auto pipeline_blend = vk::PipelineColorBlendStateCreateInfo({}, false, vk::LogicOp::eCopy, 1, &pipeline_blend_state);
    auto pipeline_dyn = vk::PipelineDynamicStateCreateInfo();

    std::array<vk::DescriptorSetLayoutBinding, 2> pipeline_layout_bind = {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer,
            1, vk::ShaderStageFlagBits::eVertex, nullptr),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler,
            1, vk::ShaderStageFlagBits::eFragment, nullptr),
    };
    auto pipeline_layout_descr_info = vk::DescriptorSetLayoutCreateInfo({}, 
        pipeline_layout_bind.size(), pipeline_layout_bind.data());
    auto pipeline_layout_descr = dev->createDescriptorSetLayoutUnique(pipeline_layout_descr_info);
    auto pipeline_layout_info = vk::PipelineLayoutCreateInfo({}, 1, &pipeline_layout_descr.get(), 0, nullptr);
    auto pipeline_layout = dev->createPipelineLayoutUnique(pipeline_layout_info);

    auto pipeline_renderpass_fb = vk::AttachmentDescription({}, vk::Format::eB8G8R8A8Unorm,
        vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
    auto pipeline_subpass_color_ref = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);
    auto pipeline_subpass = vk::SubpassDescription({}, vk::PipelineBindPoint::eGraphics, 0, nullptr, 1, &pipeline_subpass_color_ref);
    auto pipeline_renderpass_info = vk::RenderPassCreateInfo({}, 1, &pipeline_renderpass_fb, 1, &pipeline_subpass, 0, nullptr);
    auto pipeline_renderpass = dev->createRenderPassUnique(pipeline_renderpass_info);

    auto pipeline_info = vk::GraphicsPipelineCreateInfo({},
        2, pipeline_stages,
        &pipeline_vertex_input,
        &pipeline_input_assembly,
        nullptr,
        &pipeline_vpstate,
        &pipeline_raster,
        &pipeline_ms,
        nullptr,
        &pipeline_blend,
        &pipeline_dyn,
        *pipeline_layout,
        *pipeline_renderpass, 0,
        nullptr, 0);
    vk::UniquePipeline pipeline = dev->createGraphicsPipelineUnique(nullptr, pipeline_info);
    return std::tuple(std::move(pipeline), std::move(pipeline_renderpass), 
        std::move(pipeline_layout), std::move(pipeline_layout_descr));
}

std::tuple<vk::UniqueImage, vk::UniqueImageView, vk::UniqueDeviceMemory> create_depth(const vk::PhysicalDevice& pd, vk::Device const& dev, int width, int height)
{
    auto depth_format_props = pd.getFormatProperties(vk::Format::eD16Unorm);
    vk::ImageTiling depth_tiling;
    if (depth_format_props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
        depth_tiling = vk::ImageTiling::eOptimal;
    else if (depth_format_props.linearTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
        depth_tiling = vk::ImageTiling::eLinear;
    else
        throw std::runtime_error("create_depth failed to find suitable ImageTiling");
    auto depth_create_info = vk::ImageCreateInfo({}, vk::ImageType::e2D, vk::Format::eD16Unorm,
        vk::Extent3D(width, height, 1), 1, 1, vk::SampleCountFlagBits::e1,
        depth_tiling, vk::ImageUsageFlagBits::eDepthStencilAttachment);
    auto depth_image = dev.createImageUnique(depth_create_info);
    auto depth_mem_req = dev.getImageMemoryRequirements(*depth_image);
    int depth_memtype_idx = find_memory(pd, depth_mem_req, vk::MemoryPropertyFlagBits::eDeviceLocal);
    auto depth_mem = dev.allocateMemoryUnique({ depth_mem_req.size, (size_t)depth_memtype_idx });
    dev.bindImageMemory(*depth_image, *depth_mem, 0);
    auto depth_view_info = vk::ImageViewCreateInfo({}, *depth_image, vk::ImageViewType::e2D, vk::Format::eD16Unorm,
        { cs::eR, cs::eG, cs::eB, cs::eA }, { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 });
    auto depth_view = dev.createImageViewUnique(depth_view_info);
    return { std::move(depth_image), std::move(depth_view), std::move(depth_mem) };
}

auto create_texture(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev, 
    const vk::Queue& q, const vk::UniqueCommandPool& cmd_pool)
{
    glm::ivec2 pix_size;
    int pix_comp;
    auto pix_data = std::unique_ptr<uint8_t>(
        stbi_load("image.jpg", &pix_size.x, &pix_size.y, &pix_comp, 4));
    if (!pix_data || glm::any(glm::equal(pix_size, { 0, 0 })))
        throw std::runtime_error("could not create texture image.jpg");
    vk::DeviceSize pix_bytes = pix_size.x * pix_size.y * 4;

    // device image
    vk::ImageCreateInfo img_info;
    img_info.imageType = vk::ImageType::e2D;
    img_info.format = vk::Format::eR8G8B8A8Unorm;
    img_info.extent = vk::Extent3D(pix_size.x, pix_size.y, 1);
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = vk::SampleCountFlagBits::e1;
    img_info.tiling = vk::ImageTiling::eOptimal;
    img_info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
    img_info.sharingMode = vk::SharingMode::eExclusive;
    img_info.initialLayout = vk::ImageLayout::eUndefined;
    vk::UniqueImage img = dev->createImageUnique(img_info);
    vk::MemoryRequirements img_req = dev->getImageMemoryRequirements(*img);
    uint32_t img_mem_idx = find_memory(pd, img_req, vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::UniqueDeviceMemory mem = dev->allocateMemoryUnique({ img_req.size, img_mem_idx });
    dev->bindImageMemory(*img, *mem, 0);

    // image view
    vk::ImageViewCreateInfo view_info;
    view_info.image = *img;
    view_info.viewType = vk::ImageViewType::e2D;
    view_info.format = img_info.format;
    view_info.components = { cs::eR, cs::eG, cs::eB, cs::eA };
    view_info.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    vk::UniqueImageView view = dev->createImageViewUnique(view_info);

    // staging buffer
    vk::BufferCreateInfo buf_info;
    buf_info.size = pix_bytes;
    buf_info.usage = vk::BufferUsageFlagBits::eTransferSrc;
    vk::UniqueBuffer buf = dev->createBufferUnique(buf_info);
    vk::MemoryRequirements buf_req = dev->getBufferMemoryRequirements(*buf);
    uint32_t buf_mem_idx = find_memory(pd, buf_req, vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);
    vk::UniqueDeviceMemory buf_mem = dev->allocateMemoryUnique({ buf_req.size, buf_mem_idx });
    dev->bindBufferMemory(*buf, *buf_mem, 0);
    uint8_t* buf_ptr = reinterpret_cast<uint8_t*>(dev->mapMemory(*buf_mem, 0, pix_bytes));
    std::copy_n(pix_data.get(), pix_bytes, buf_ptr);
    dev->unmapMemory(*buf_mem);

    vk::UniqueCommandBuffer cmd = std::move(dev->allocateCommandBuffersUnique(
        { *cmd_pool, vk::CommandBufferLevel::ePrimary, 1 }).front());
    cmd->begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    {
        vk::ImageMemoryBarrier imb;
        imb.srcAccessMask = vk::AccessFlags();
        imb.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        imb.oldLayout = vk::ImageLayout::eUndefined;
        imb.newLayout = vk::ImageLayout::eTransferDstOptimal;
        imb.srcQueueFamilyIndex = imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.image = *img;
        imb.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 0, nullptr, 1, &imb);
        
        vk::BufferImageCopy bic;
        bic.bufferOffset = 0;
        bic.bufferRowLength = pix_size.x;
        bic.bufferImageHeight = pix_size.y;
        bic.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
        bic.imageOffset = vk::Offset3D();
        bic.imageExtent = vk::Extent3D(pix_size.x, pix_size.y, 1);
        cmd->copyBufferToImage(*buf, *img, vk::ImageLayout::eTransferDstOptimal, bic);
        
        imb.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        imb.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        imb.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        imb.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imb.srcQueueFamilyIndex = imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.image = *img;
        imb.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader, {}, 0, nullptr, 0, nullptr, 1, &imb);
    }
    cmd->end();

    vk::SubmitInfo si;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd.get();
    vk::UniqueFence submit_fence = dev->createFenceUnique(vk::FenceCreateInfo());
    q.submit(si, *submit_fence);
    dev->waitForFences(*submit_fence, true, UINT64_MAX);

    return std::tuple(std::move(img), std::move(mem), std::move(view));
}

vk::UniqueSampler create_sampler(const vk::UniqueDevice& dev)
{
    vk::SamplerCreateInfo info;
    info.magFilter = vk::Filter::eLinear;
    info.minFilter = vk::Filter::eLinear;
    info.mipmapMode = vk::SamplerMipmapMode::eLinear;
    info.addressModeU = vk::SamplerAddressMode::eRepeat;
    info.addressModeV = vk::SamplerAddressMode::eRepeat;
    info.addressModeW = vk::SamplerAddressMode::eRepeat;
    info.mipLodBias = 0.f;
    info.anisotropyEnable = true;
    info.maxAnisotropy = 1.f;
    info.compareEnable = false;
    info.compareOp = vk::CompareOp::eAlways;
    info.minLod = 0.f;
    info.maxLod = 0.f;
    info.borderColor = vk::BorderColor::eIntOpaqueBlack;
    info.unnormalizedCoordinates = false;
    return dev->createSamplerUnique(info);
}

auto create_triangle(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev)
{
    constexpr std::array<vertex_t, 4> triangle = {
        vertex_t{{-1.f, 1.f}, {1.0f, 1.0f, 1.0f}, {0, 0}},
        vertex_t{{-1.f,-1.f}, {0.0f, 1.0f, 0.0f}, {0, 1}},
        vertex_t{{ 1.f,-1.f}, {0.0f, 0.0f, 1.0f}, {1, 1}},
        vertex_t{{ 1.f, 1.f}, {1.0f, 0.0f, 0.0f}, {1, 0}},
    };
    auto vbo_info = vk::BufferCreateInfo({}, sizeof(triangle), vk::BufferUsageFlagBits::eVertexBuffer,
        vk::SharingMode::eExclusive, 0, nullptr);
    auto vbo_buffer = dev->createBufferUnique(vbo_info);
    auto vbo_mem_req = dev->getBufferMemoryRequirements(*vbo_buffer);
    auto vbo_mem_idx = find_memory(pd, vbo_mem_req,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    auto vbo_mem = dev->allocateMemoryUnique(vk::MemoryAllocateInfo(vbo_mem_req.size, vbo_mem_idx));
    dev->bindBufferMemory(*vbo_buffer, *vbo_mem, 0);
    if (auto vbo_map = static_cast<vertex_t*>(dev->mapMemory(*vbo_mem, 0, sizeof(triangle))))
    {
        std::copy(triangle.begin(), triangle.end(), vbo_map);
        dev->unmapMemory(*vbo_mem);
    }

    constexpr std::array<uint32_t, 6> indices = { 0, 1, 2, 0, 2, 3 };
    auto ibo_info = vk::BufferCreateInfo({}, sizeof(indices), vk::BufferUsageFlagBits::eIndexBuffer,
        vk::SharingMode::eExclusive, 0, nullptr);
    auto ibo_buffer = dev->createBufferUnique(ibo_info);
    auto ibo_mem_req = dev->getBufferMemoryRequirements(*ibo_buffer);
    auto ibo_mem_idx = find_memory(pd, ibo_mem_req,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    auto ibo_mem = dev->allocateMemoryUnique(vk::MemoryAllocateInfo(ibo_mem_req.size, ibo_mem_idx));
    dev->bindBufferMemory(*ibo_buffer, *ibo_mem, 0);
    if (auto ibo_map = static_cast<uint32_t*>(dev->mapMemory(*ibo_mem, 0, sizeof(indices))))
    {
        std::copy(indices.begin(), indices.end(), ibo_map);
        dev->unmapMemory(*ibo_mem);
    }

    return std::tuple(std::move(vbo_buffer), std::move(vbo_mem),
        std::move(ibo_buffer), std::move(ibo_mem));
}

auto create_uniforms(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev)
{
    auto uniform_buffer_info = vk::BufferCreateInfo({}, sizeof(glm::mat4), vk::BufferUsageFlagBits::eUniformBuffer);
    auto uniform_buffer = dev->createBufferUnique(uniform_buffer_info);
    auto uniform_mem_req = dev->getBufferMemoryRequirements(*uniform_buffer);
    auto uniform_mem_idx = find_memory(pd, uniform_mem_req, 
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    auto uniform_mem = dev->allocateMemoryUnique({ uniform_mem_req.size, (uint32_t)uniform_mem_idx });
    dev->bindBufferMemory(*uniform_buffer, *uniform_mem, 0);
    return std::tuple(std::move(uniform_buffer), std::move(uniform_mem));
}

void update_uniforms(const vk::UniqueDevice& dev, const vk::UniqueDeviceMemory& uniform_mem)
{
    static float theta = 0;
    theta += glm::radians(1.f);
    glm::mat4 model = glm::eulerAngleZ(theta);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, -5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    // vulkan clip space has inverted y and half z !
    glm::mat4 clip = glm::mat4(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.0f, 0.0f, 0.5f, 1.0f);
    glm::mat4 mvpc = clip * projection * view * model;
    if (auto uniform_map = static_cast<glm::mat4*>(dev->mapMemory(*uniform_mem, 0, VK_WHOLE_SIZE)))
    {
        std::copy_n(&mvpc, 1, uniform_map);
        dev->unmapMemory(*uniform_mem);
    }
}

int main()
{
    // create vulkan instance
    vk::ApplicationInfo app_info("VulkanTest", VK_MAKE_VERSION(0, 0, 1), "VulkanEngine", 1, VK_API_VERSION_1_1);
    std::vector<const char*> inst_layers{
        "VK_LAYER_LUNARG_standard_validation",
        "VK_LAYER_KHRONOS_validation",
        //"VK_LAYER_LUNARG_api_dump",
        "VK_LAYER_RENDERDOC_Capture",
    };
    std::vector<const char*> inst_ext{
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };
    vk::InstanceCreateInfo create_info({}, &app_info,
        inst_layers.size(), inst_layers.data(), inst_ext.size(), inst_ext.data());
    auto inst = vk::createInstanceUnique(create_info);

    for (auto dl : vk::enumerateInstanceLayerProperties())
    {
        std::cout << "instance layer " << dl.layerName << ": " << dl.description << "\n";
    }

    init_debug_message(inst);

    auto wnd = create_window(800, 600);
    auto surf_info = vk::Win32SurfaceCreateInfoKHR({}, GetModuleHandle(0), wnd);
    auto surf = inst->createWin32SurfaceKHRUnique(surf_info);

    auto devices = inst->enumeratePhysicalDevices();
    for (auto& pd : devices)
    {
        for (auto dl : pd.enumerateDeviceLayerProperties())
        {
            std::cout << "device layer " << dl.layerName << ": " << dl.description << "\n";
        }
        auto props = pd.getProperties();
        std::cout << "device " << props.deviceName << "\n";

        auto qf_props = pd.getQueueFamilyProperties();
        for (int idx = 0; idx < qf_props.size(); idx++)
        {
            if (qf_props[idx].queueFlags & vk::QueueFlagBits::eGraphics && pd.getSurfaceSupportKHR(idx, *surf))
            {
                float priority = 0.0f;
                auto queue_info = vk::DeviceQueueCreateInfo({}, idx, 1u, &priority);
                std::vector<const char*> inst_layers;
                std::vector<const char*> inst_ext{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
                vk::PhysicalDeviceFeatures dev_feat;
                dev_feat.samplerAnisotropy = true;
                auto dev_info = vk::DeviceCreateInfo({}, 1, &queue_info,
                    inst_layers.size(), inst_layers.data(), inst_ext.size(), inst_ext.data(), &dev_feat);
                auto dev = pd.createDeviceUnique(dev_info);
                if (dev)
                {
                    auto q = dev->getQueue(idx, 0);
                    auto surface_formats = pd.getSurfaceFormatsKHR(*surf);
                    auto surface_caps = pd.getSurfaceCapabilitiesKHR(*surf);
                    auto swap_info = vk::SwapchainCreateInfoKHR({}, *surf, surface_caps.minImageCount,
                        vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear, surface_caps.currentExtent, 1, 
                        vk::ImageUsageFlagBits::eColorAttachment, vk::SharingMode::eExclusive, 0, nullptr, 
                        vk::SurfaceTransformFlagBitsKHR::eIdentity, vk::CompositeAlphaFlagBitsKHR::eOpaque, 
                        vk::PresentModeKHR::eFifo, true, nullptr);
                    auto swapchain = dev->createSwapchainKHRUnique(swap_info);

                    auto vert_module = load_shader(*dev, "shader.vert.spv");
                    auto frag_module = load_shader(*dev, "shader.frag.spv");
                    auto [pipeline, renderpass, layout, layout_descr] = init_pipeline(dev, 
                        vert_module, frag_module, surface_caps.currentExtent);

                    auto cmd_pool_info = vk::CommandPoolCreateInfo({}, idx);
                    auto cmd_pool = dev->createCommandPoolUnique(cmd_pool_info);
                    std::array<vk::DescriptorPoolSize, 2> descr_pool_size = {
                        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1),
                        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1),
                    };
                    auto descr_pool_info = vk::DescriptorPoolCreateInfo({}, 2, descr_pool_size.size(), descr_pool_size.data());
                    auto descr_pool = dev->createDescriptorPoolUnique(descr_pool_info);

                    auto [tex_img, tex_mem, tex_view] = create_texture(pd, dev, q, cmd_pool);
                    auto sampler = create_sampler(dev);

                    auto [triangle_buffer, triangle_mem, triangle_ibo, triangle_ibo_mem] = create_triangle(pd, dev);
                    auto [uniform_buffer, uniform_mem] = create_uniforms(pd, dev);

                    auto sc_images = dev->getSwapchainImagesKHR(*swapchain);
                    std::vector<vk::UniqueImageView> sc_image_views(sc_images.size());
                    std::vector<vk::UniqueFramebuffer> framebuffers(sc_images.size());
                    std::vector<vk::UniqueCommandBuffer> cmd(sc_images.size());
                    std::vector<vk::UniqueDescriptorSet> descr(sc_images.size());
                    for (size_t image_index = 0; image_index < sc_images.size(); image_index++)
                    {
                        auto view_info = vk::ImageViewCreateInfo({}, sc_images[image_index],
                            vk::ImageViewType::e2D, vk::Format::eB8G8R8A8Unorm,
                            vk::ComponentMapping(cs::eR, cs::eG, cs::eB, cs::eA),
                            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
                        sc_image_views[image_index] = dev->createImageViewUnique(view_info);

                        auto fb_info = vk::FramebufferCreateInfo({}, *renderpass, 1, &sc_image_views[image_index].get(),
                            surface_caps.currentExtent.width, surface_caps.currentExtent.height, 1);
                        framebuffers[image_index] = dev->createFramebufferUnique(fb_info);

                        auto cmd_info = vk::CommandBufferAllocateInfo(*cmd_pool, vk::CommandBufferLevel::ePrimary, 1);
                        cmd[image_index] = std::move(dev->allocateCommandBuffersUnique(cmd_info).front());

                        auto descr_info = vk::DescriptorSetAllocateInfo(*descr_pool, 1, &layout_descr.get());
                        descr[image_index] = std::move(dev->allocateDescriptorSetsUnique(descr_info).front());
                        auto descr_buffer_info = vk::DescriptorBufferInfo(*uniform_buffer, 0, VK_WHOLE_SIZE);
                        auto descr_image_info = vk::DescriptorImageInfo(*sampler, 
                            *tex_view, vk::ImageLayout::eShaderReadOnlyOptimal);
                        std::array<vk::WriteDescriptorSet, 2> descr_write = {
                            vk::WriteDescriptorSet(*descr[image_index], 0, 0, 1,
                                vk::DescriptorType::eUniformBuffer, nullptr, &descr_buffer_info, nullptr),
                            vk::WriteDescriptorSet(*descr[image_index], 1, 0, 1,
                                vk::DescriptorType::eCombinedImageSampler, &descr_image_info, nullptr, nullptr),
                        };
                        dev->updateDescriptorSets(descr_write, nullptr);

                        vk::ClearValue clearColor(std::array<float, 4>{ 0.3f, 0.0f, 0.0f, 1.0f });
                        auto begin_info = vk::RenderPassBeginInfo(*renderpass, *framebuffers[image_index],
                            vk::Rect2D({ 0, 0 }, surface_caps.currentExtent), 1, &clearColor);

                        cmd[image_index]->begin(vk::CommandBufferBeginInfo());
                        cmd[image_index]->beginRenderPass(begin_info, vk::SubpassContents::eInline);
                        cmd[image_index]->bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
                        cmd[image_index]->bindVertexBuffers(0, *triangle_buffer, { 0 });
                        cmd[image_index]->bindIndexBuffer(*triangle_ibo, 0, vk::IndexType::eUint32);
                        cmd[image_index]->bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            *layout, 0, *descr[image_index], nullptr);
                        cmd[image_index]->drawIndexed(6, 1, 0, 0, 0);
                        cmd[image_index]->endRenderPass();
                        cmd[image_index]->end();
                    }

                    // Create Depth buffer
                    //auto [depth_image, depth_view, depth_mem] = create_depth(pd, *dev, 0, 0);


                    MSG msg;
                    vk::UniqueSemaphore render_finished_sem = dev->createSemaphoreUnique(vk::SemaphoreCreateInfo());
                    while (true)
                    {
                        if (PeekMessage(&msg, wnd, 0, 0, PM_REMOVE))
                        {
                            TranslateMessage(&msg);
                            DispatchMessage(&msg);
                        }

                        update_uniforms(dev, uniform_mem);

                        auto swapchain_sem = dev->createSemaphoreUnique(vk::SemaphoreCreateInfo());
                        auto swapchain_idx = dev->acquireNextImageKHR(*swapchain, UINT64_MAX, *swapchain_sem, nullptr);
                        vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eTopOfPipe;
                        auto submit_info = vk::SubmitInfo(1, &swapchain_sem.get(), &wait_stage, 1, &cmd[swapchain_idx.value].get(), 1, &render_finished_sem.get());
                        auto fence = dev->createFenceUnique(vk::FenceCreateInfo());
                        q.submit(submit_info, *fence);

                        auto present_info = vk::PresentInfoKHR(1, &render_finished_sem.get(), 1, &swapchain.get(), &swapchain_idx.value);
                        q.presentKHR(present_info);
                        dev->waitForFences(*fence, true, UINT64_MAX);
                    }
                    dev->waitIdle();
                    return 0;
                }
                else
                {
                    std::cout << "createDeviceUnique failed\n";
                }
            }
        }
    }
}
