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
