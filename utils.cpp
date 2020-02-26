#include "pch.h"
#include "utils.h"

int find_memory(const vk::PhysicalDevice& pd, const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags)
{
    vk::PhysicalDeviceMemoryProperties mp = pd.getMemoryProperties();
    for (size_t mem_i = 0; mem_i < mp.memoryTypeCount; mem_i++)
        if ((1 << mem_i) & req.memoryTypeBits && (mp.memoryTypes[mem_i].propertyFlags & flags) == flags)
            return mem_i;
    throw std::runtime_error("find_memory failed");
    return -1;
}

std::vector<glm::uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs)
        throw std::runtime_error("read_file failed to open the file " + path.string());
    std::vector<uint8_t> buffer(ifs.tellg());
    ifs.seekg(0, std::ios::beg);
    ifs.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    return buffer;
}

vk::UniqueShaderModule load_shader(const vk::UniqueDevice& dev, const std::filesystem::path& path)
{
    auto code = read_file(path);
    return dev->createShaderModuleUnique({ {}, code.size(),
        reinterpret_cast<uint32_t*>(code.data()) });
}

bool swapchain_needs_recreation = false;
glm::ivec2 cur_pos;
glm::ivec2 wnd_size;
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
        RECT r;
        switch (uMsg)
        {
        case WM_CLOSE:
            exit(0);
        case WM_SIZE:
            swapchain_needs_recreation = true;
            GetClientRect(hWnd, &r);
            wnd_size.x = r.right - r.left;
            wnd_size.y = r.bottom - r.top;
            break;
        case WM_MOUSEMOVE:
            cur_pos.x = GET_X_LPARAM(lParam);
            cur_pos.y = GET_Y_LPARAM(lParam);
            break;
        default:
            break;
        }
        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
    };
    if (!RegisterClassA(&wc))
        exit(1);
    RECT r = { 0, 0, width, height };
    wnd_size.x = r.right - r.left;
    wnd_size.y = r.bottom - r.top;
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, false);
    return CreateWindowA(wc.lpszClassName, "Vulkan", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_SYSMENU, 0, 0,
        r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
}

std::tuple<vk::UniqueImage, vk::UniqueImageView, vk::UniqueDeviceMemory>
create_depth(const vk::PhysicalDevice& pd, vk::Device const& dev, int width, int height)
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
    return std::tuple(std::move(depth_image), std::move(depth_view), std::move(depth_mem));
}

std::tuple<vk::UniqueImage, vk::UniqueDeviceMemory, vk::UniqueImageView>
create_texture(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev, const vk::Queue& q, const vk::UniqueCommandPool& cmd_pool)
{
    glm::ivec2 pix_size;
    int pix_comp;
    auto pix_data = std::unique_ptr<uint8_t>(
        stbi_load("brush.png", &pix_size.x, &pix_size.y, &pix_comp, 4));
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
        vertex_t{{-1.f, 1.f, 0.f}, {1.0f, 1.0f, 1.0f}, {0, 1}},
        vertex_t{{-1.f,-1.f, 0.f}, {0.0f, 1.0f, 0.0f}, {0, 0}},
        vertex_t{{ 1.f,-1.f, 0.f}, {0.0f, 0.0f, 1.0f}, {1, 0}},
        vertex_t{{ 1.f, 1.f, 0.f}, {1.0f, 0.0f, 0.0f}, {1, 1}},
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
        std::move(ibo_buffer), std::move(ibo_mem), indices.size());
}
