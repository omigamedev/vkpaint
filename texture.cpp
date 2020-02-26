#include "pch.h"
#include "texture.h"

bool Texture::create(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev, const vk::Queue& q, const vk::UniqueCommandPool& cmd_pool, const std::filesystem::path& path)
{
    glm::ivec2 pix_size;
    int pix_comp;
    auto pix_data = std::unique_ptr<uint8_t>(
        stbi_load(path.string().c_str(), &pix_size.x, &pix_size.y, &pix_comp, 4));
    if (!pix_data || glm::any(glm::equal(pix_size, { 0, 0 })))
        throw std::runtime_error("could not create texture image.jpg");
    vk::DeviceSize pix_bytes = pix_size.x * pix_size.y * 4ull;

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
    m_img = dev->createImageUnique(img_info);
    vk::MemoryRequirements img_req = dev->getImageMemoryRequirements(*m_img);
    uint32_t img_mem_idx = find_memory(pd, img_req, vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_mem = dev->allocateMemoryUnique({ img_req.size, img_mem_idx });
    dev->bindImageMemory(*m_img, *m_mem, 0);

    // image view
    vk::ImageViewCreateInfo view_info;
    view_info.image = *m_img;
    view_info.viewType = vk::ImageViewType::e2D;
    view_info.format = img_info.format;
    view_info.components = { cs::eR, cs::eG, cs::eB, cs::eA };
    view_info.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    m_view = dev->createImageViewUnique(view_info);

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
        imb.image = *m_img;
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
        cmd->copyBufferToImage(*buf, *m_img, vk::ImageLayout::eTransferDstOptimal, bic);

        imb.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        imb.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        imb.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        imb.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imb.srcQueueFamilyIndex = imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.image = *m_img;
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

    return true;
}
