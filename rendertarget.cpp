#include "pch.h"
#include "rendertarget.h"
#include "utils.h"
#include "debug_message.h"

/*
Canvas: where we are going to draw stuff
- Color attachment: RGBA Image
- Framebuffer
- Pipeline:
-- input: no input
-- blend: no blend
- Shader: mixes the brush samples
-- tex: color attachment (base mixing)
-- tex: brush value
--
*/

bool RenderTarget::create(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev, int width, int height, vk::SampleCountFlagBits samples, vk::Format format)
{
    m_size = { width, height };
    m_samples = samples;
    m_format = format;

    create_framebuffer(pd, dev);

    uint32_t spec_samples_count = (uint32_t)samples;
    vk::SpecializationMapEntry spec_samples;
    spec_samples.constantID = 0;
    spec_samples.offset = 0;
    spec_samples.size = sizeof(uint32_t);
    vk::SpecializationInfo spec_info;
    spec_info.mapEntryCount = 1;
    spec_info.pMapEntries = &spec_samples;
    spec_info.dataSize = sizeof(spec_samples_count);
    spec_info.pData = &spec_samples_count;
    if (samples == vk::SampleCountFlagBits::e1)
    {
        m_shader_vert = load_shader(dev, "shader.vert.spv");
        m_shader_frag = load_shader(dev, "shader.frag.spv");
    }
    else
    {
        m_shader_vert = load_shader(dev, "shader.vert.spv");
        m_shader_frag = load_shader(dev, "shader.frag.ms.spv");
    }
    std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *m_shader_vert, "main"),
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *m_shader_frag, "main", &spec_info),
    };

    std::array<vk::DescriptorSetLayoutBinding, 4> pipeline_layout_bind = {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, // mvp
            1, vk::ShaderStageFlagBits::eVertex, nullptr),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, // tex_bg
            1, vk::ShaderStageFlagBits::eFragment, nullptr),
        vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eCombinedImageSampler, // tex_brush
            1, vk::ShaderStageFlagBits::eFragment, nullptr),
        vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eUniformBuffer, // col
            1, vk::ShaderStageFlagBits::eFragment, nullptr),
    };
    vk::DescriptorSetLayoutCreateInfo descr_info;
    descr_info.bindingCount = pipeline_layout_bind.size();
    descr_info.pBindings = pipeline_layout_bind.data();
    m_descr_layout = dev->createDescriptorSetLayoutUnique(descr_info);
    debug_name(m_descr_layout, "RenderTarget::m_descr_layout");

    vk::PipelineLayoutCreateInfo layout_info;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &m_descr_layout.get();
    m_layout = dev->createPipelineLayoutUnique(layout_info);
    debug_name(m_layout, "RenderTarget::m_layout");

    vk::PipelineVertexInputStateCreateInfo vertex_input;
    vk::PipelineInputAssemblyStateCreateInfo input_assembly;
    input_assembly.topology = vk::PrimitiveTopology::eTriangleList;

    vk::Viewport vp = { 0.f, 0.f, (float)m_size.x, (float)m_size.y, 0.f, 1.f };
    vk::Rect2D scissor = { vk::Offset2D(0), vk::Extent2D(m_size.x, m_size.y) };
    vk::PipelineViewportStateCreateInfo viewport;
    viewport.viewportCount = 1;
    viewport.pViewports = &vp;
    viewport.scissorCount = 1;
    viewport.pScissors = &scissor;

    vk::PipelineRasterizationStateCreateInfo rasterization;
    rasterization.depthBiasClamp = false;
    rasterization.rasterizerDiscardEnable = false;
    rasterization.polygonMode = vk::PolygonMode::eFill;
    rasterization.cullMode = vk::CullModeFlagBits::eNone;
    rasterization.frontFace = vk::FrontFace::eClockwise;
    rasterization.depthBiasEnable = false;
    rasterization.lineWidth = 1.f;

    vk::PipelineMultisampleStateCreateInfo multisample;
    multisample.rasterizationSamples = m_samples;
    multisample.sampleShadingEnable = true;
    multisample.minSampleShading = (m_samples == vk::SampleCountFlagBits::e1) ? 1.f : .25f;

    vk::PipelineColorBlendAttachmentState blend_color;
    blend_color.blendEnable = false;
    blend_color.colorWriteMask = cc::eR | cc::eG | cc::eB | cc::eA;
    vk::PipelineColorBlendStateCreateInfo blend;
    blend.logicOpEnable = false;
    blend.logicOp = vk::LogicOp::eCopy;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_color;

    vk::GraphicsPipelineCreateInfo info;
    info.stageCount = stages.size();
    info.pStages = stages.data();
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pTessellationState = nullptr;
    info.pViewportState = &viewport;
    info.pRasterizationState = &rasterization;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = nullptr;
    info.pColorBlendState = &blend;
    info.pDynamicState = nullptr;
    info.layout = *m_layout;
    info.renderPass = *m_renderpass;
    info.subpass = 0;

    m_pipeline = dev->createGraphicsPipelineUnique(nullptr, info);
    debug_name(m_pipeline, "RenderTarget::m_pipeline");


    return true;
}

void RenderTarget::to_layout(const vk::UniqueDevice& dev, const vk::UniqueCommandPool& cmd_pool, const vk::Queue& q,
    vk::AccessFlags access_mask, vk::ImageLayout layout, vk::PipelineStageFlags src_stage, vk::PipelineStageFlags dst_stage)
{
    vk::UniqueCommandBuffer cmd = std::move(dev->allocateCommandBuffersUnique(
        { *cmd_pool, vk::CommandBufferLevel::ePrimary, 1 }).front());
    debug_name(cmd, "RenderTarget::to_layout::cmd");
    cmd->begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    {
        vk::ImageMemoryBarrier imb;
        imb.srcAccessMask = m_fb_access_mask;
        imb.dstAccessMask = access_mask;
        imb.oldLayout = m_fb_layout;
        imb.newLayout = layout;
        imb.srcQueueFamilyIndex = imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.image = *m_fb_img;
        imb.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        cmd->pipelineBarrier(src_stage, dst_stage, {}, 0, nullptr, 0, nullptr, 1, &imb);
    }
    cmd->end();

    m_fb_access_mask = access_mask;
    m_fb_layout = layout;

    vk::SubmitInfo si;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd.get();
    vk::UniqueFence submit_fence = dev->createFenceUnique(vk::FenceCreateInfo());
    q.submit(si, *submit_fence);
    dev->waitForFences(*submit_fence, true, UINT64_MAX);
}

void RenderTarget::resolve(const vk::UniqueDevice& m_dev, const vk::UniqueCommandPool& cmd_pool, const vk::Queue& cmd_queue)
{
    vk::SubmitInfo si;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_resolve.get();
    vk::UniqueFence submit_fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
    cmd_queue.submit(si, *submit_fence);
    m_dev->waitForFences(*submit_fence, true, UINT64_MAX);
}

bool RenderTarget::create_framebuffer(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev)
{
    // device image
    vk::ImageCreateInfo img_info;
    img_info.imageType = vk::ImageType::e2D;
    img_info.format = m_format;
    img_info.extent = vk::Extent3D(m_size.x, m_size.y, 1);
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = m_samples;
    img_info.tiling = vk::ImageTiling::eOptimal;
    img_info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
    img_info.sharingMode = vk::SharingMode::eExclusive; // TODO: check this since it will likely be used in different command buffers
    img_info.initialLayout = vk::ImageLayout::eUndefined;
    m_fb_img = dev->createImageUnique(img_info);
    debug_name(m_fb_img, "RenderTarget::m_fb_img");

    vk::ImageCreateInfo resolved_info;
    resolved_info.imageType = vk::ImageType::e2D;
    resolved_info.format = vk::Format::eR8G8B8A8Unorm;
    resolved_info.extent = vk::Extent3D(m_size.x, m_size.y, 1);
    resolved_info.mipLevels = 1;
    resolved_info.arrayLayers = 1;
    resolved_info.samples = vk::SampleCountFlagBits::e1;
    resolved_info.tiling = vk::ImageTiling::eOptimal;
    resolved_info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;
    resolved_info.sharingMode = vk::SharingMode::eExclusive;
    resolved_info.initialLayout = vk::ImageLayout::eUndefined;
    m_resolved_img = dev->createImageUnique(resolved_info);
    debug_name(m_resolved_img, "RenderTarget::m_resolved_img");
    vk::MemoryRequirements img_req = dev->getImageMemoryRequirements(*m_fb_img);
    uint32_t img_mem_idx = find_memory(pd, img_req, vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::MemoryRequirements resolved_req = dev->getImageMemoryRequirements(*m_resolved_img);
    uint32_t resolved_mem_idx = find_memory(pd, resolved_req, vk::MemoryPropertyFlagBits::eDeviceLocal);
    if (img_mem_idx == resolved_mem_idx)
    {
        m_fb_mem = dev->allocateMemoryUnique({ img_req.size + resolved_req.size, img_mem_idx });
        debug_name(m_fb_mem, "RenderTarget::m_fb_mem");
        m_resolved_mem.reset();
        dev->bindImageMemory(*m_fb_img, *m_fb_mem, 0);
        dev->bindImageMemory(*m_resolved_img, *m_fb_mem, img_req.size);
    }
    else
    {
        m_fb_mem = dev->allocateMemoryUnique({ img_req.size, img_mem_idx });
        debug_name(m_fb_mem, "RenderTarget::m_fb_mem");
        dev->bindImageMemory(*m_fb_img, *m_fb_mem, 0);
        m_resolved_mem = dev->allocateMemoryUnique({ resolved_req.size, resolved_mem_idx });
        debug_name(m_resolved_mem, "RenderTarget::m_resolved_mem");
        dev->bindImageMemory(*m_resolved_img, *m_resolved_mem, 0);
    }

    // image view
    vk::ImageViewCreateInfo view_info;
    view_info.image = *m_fb_img;
    view_info.viewType = vk::ImageViewType::e2D;
    view_info.format = img_info.format;
    view_info.components = { cs::eR, cs::eG, cs::eB, cs::eA };
    view_info.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    m_fb_view = dev->createImageViewUnique(view_info);
    debug_name(m_fb_view, "RenderTarget::m_fb_view");
    vk::ImageViewCreateInfo resolved_view_info;
    resolved_view_info.image = *m_resolved_img;
    resolved_view_info.viewType = vk::ImageViewType::e2D;
    resolved_view_info.format = resolved_info.format;
    resolved_view_info.components = { cs::eR, cs::eG, cs::eB, cs::eA };
    resolved_view_info.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    m_resolved_view = dev->createImageViewUnique(resolved_view_info);
    debug_name(m_resolved_view, "RenderTarget::m_resolved_view");

    // renderpass
    vk::AttachmentDescription renderpass_descr;
    renderpass_descr.format = img_info.format;
    renderpass_descr.samples = m_samples;
    renderpass_descr.loadOp = vk::AttachmentLoadOp::eLoad;
    renderpass_descr.storeOp = vk::AttachmentStoreOp::eStore;
    renderpass_descr.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    renderpass_descr.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    renderpass_descr.initialLayout = vk::ImageLayout::eColorAttachmentOptimal; // TODO: try other things
    renderpass_descr.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    auto subpass_color_ref = vk::AttachmentReference(0, vk::ImageLayout::eGeneral);
    vk::SubpassDescription subpass_descr;
    subpass_descr.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass_descr.colorAttachmentCount = 1;
    subpass_descr.pColorAttachments = &subpass_color_ref;

    vk::RenderPassCreateInfo renderpass_info;
    renderpass_info.attachmentCount = 1;
    renderpass_info.pAttachments = &renderpass_descr;
    renderpass_info.subpassCount = 1;
    renderpass_info.pSubpasses = &subpass_descr;
    m_renderpass = dev->createRenderPassUnique(renderpass_info);
    debug_name(m_renderpass, "RenderTarget::m_renderpass");

    vk::FramebufferCreateInfo fb_info;
    fb_info.renderPass = *m_renderpass;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &m_fb_view.get();
    fb_info.width = m_size.x;
    fb_info.height = m_size.y;
    fb_info.layers = 1;
    m_framebuffer = dev->createFramebufferUnique(fb_info);
    debug_name(m_framebuffer, "RenderTarget::m_framebuffer");

    m_fb_access_mask = vk::AccessFlags();
    m_fb_layout = vk::ImageLayout::eUndefined;

    return true;
}

bool RenderTarget::create_resolver(const vk::UniqueDevice& m_dev, const vk::UniqueCommandPool& cmd_pool, const vk::Queue& cmd_queue)
{
    auto cmd_resolve_info = vk::CommandBufferAllocateInfo(*cmd_pool, vk::CommandBufferLevel::ePrimary, 1);
    cmd_resolve = std::move(m_dev->allocateCommandBuffersUnique(cmd_resolve_info).front());
    debug_name(cmd_resolve, "RenderTarget::cmd_resolve");

    cmd_resolve->begin(vk::CommandBufferBeginInfo());

    vk::ImageMemoryBarrier imb;
    imb.srcQueueFamilyIndex = imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.image = *m_resolved_img;
    imb.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    imb.srcAccessMask = {};// vk::AccessFlagBits::eShaderRead;
    imb.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    imb.oldLayout = vk::ImageLayout::eUndefined;
    imb.newLayout = vk::ImageLayout::eTransferDstOptimal;
    cmd_resolve->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
        {}, 0, nullptr, 0, nullptr, 1, &imb);

    vk::ImageResolve resolve;
    resolve.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    resolve.srcOffset = vk::Offset3D();
    resolve.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
    resolve.dstOffset = vk::Offset3D();
    resolve.extent = vk::Extent3D(m_size.x, m_size.y, 1);
    cmd_resolve->resolveImage(*m_fb_img, vk::ImageLayout::eGeneral,
        *m_resolved_img, vk::ImageLayout::eTransferDstOptimal, resolve);

    imb.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    imb.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    imb.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    imb.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    cmd_resolve->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
        {}, 0, nullptr, 0, nullptr, 1, &imb);

    cmd_resolve->end();

    // transition layout

    vk::UniqueCommandBuffer cmd = std::move(m_dev->allocateCommandBuffersUnique(
        { *cmd_pool, vk::CommandBufferLevel::ePrimary, 1 }).front());
    debug_name(cmd, "RenderTarget::create_resolver::cmd");

    cmd->begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    {
        vk::ImageMemoryBarrier imb;
        imb.srcAccessMask = {};
        imb.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        imb.oldLayout = vk::ImageLayout::eUndefined;
        imb.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imb.srcQueueFamilyIndex = imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imb.image = *m_resolved_img;
        imb.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader,
            {}, 0, nullptr, 0, nullptr, 1, &imb);
    }
    cmd->end();

    vk::SubmitInfo si;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd.get();
    vk::UniqueFence submit_fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
    cmd_queue.submit(si, *submit_fence);
    m_dev->waitForFences(*submit_fence, true, UINT64_MAX);

    return true;
}
