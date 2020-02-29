#include "pch.h"
#include "rendertarget.h"
#include "utils.h"

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

bool RenderTarget::create(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev, int width, int height)
{
    m_size = { width, height };

    create_framebuffer(pd, dev);

    vk::UniqueShaderModule shader_vert = load_shader(dev, "shader.vert.spv");
    vk::UniqueShaderModule shader_frag = load_shader(dev, "shader.frag.spv");
    std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *shader_vert, "main"),
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *shader_frag, "main"),
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

    vk::PipelineLayoutCreateInfo layout_info;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &m_descr_layout.get();
    m_layout = dev->createPipelineLayoutUnique(layout_info);

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
    multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;
    multisample.sampleShadingEnable = false;
    multisample.minSampleShading = 1.f;

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

    return true;
}

void RenderTarget::to_layout(const vk::UniqueDevice& dev, const vk::UniqueCommandPool& cmd_pool, const vk::Queue& q,
    vk::AccessFlags access_mask, vk::ImageLayout layout, vk::PipelineStageFlags src_stage, vk::PipelineStageFlags dst_stage)
{
    vk::UniqueCommandBuffer cmd = std::move(dev->allocateCommandBuffersUnique(
        { *cmd_pool, vk::CommandBufferLevel::ePrimary, 1 }).front());
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

bool RenderTarget::create_framebuffer(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev)
{
    // device image
    vk::ImageCreateInfo img_info;
    img_info.imageType = vk::ImageType::e2D;
    img_info.format = vk::Format::eR32G32B32A32Sfloat;
    img_info.extent = vk::Extent3D(m_size.x, m_size.y, 1);
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = vk::SampleCountFlagBits::e1;
    img_info.tiling = vk::ImageTiling::eOptimal;
    img_info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
    img_info.sharingMode = vk::SharingMode::eExclusive; // TODO: check this since it will likely be used in different command buffers
    img_info.initialLayout = vk::ImageLayout::eUndefined;
    m_fb_img = dev->createImageUnique(img_info);
    vk::MemoryRequirements img_req = dev->getImageMemoryRequirements(*m_fb_img);
    uint32_t img_mem_idx = find_memory(pd, img_req, vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_fb_mem = dev->allocateMemoryUnique({ img_req.size, img_mem_idx });
    dev->bindImageMemory(*m_fb_img, *m_fb_mem, 0);

    // image view
    vk::ImageViewCreateInfo view_info;
    view_info.image = *m_fb_img;
    view_info.viewType = vk::ImageViewType::e2D;
    view_info.format = img_info.format;
    view_info.components = { cs::eR, cs::eG, cs::eB, cs::eA };
    view_info.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    m_fb_view = dev->createImageViewUnique(view_info);

    // renderpass
    vk::AttachmentDescription renderpass_descr;
    renderpass_descr.format = img_info.format;
    renderpass_descr.samples = vk::SampleCountFlagBits::e1;
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

    vk::FramebufferCreateInfo fb_info;
    fb_info.renderPass = *m_renderpass;
    fb_info.attachmentCount = 1;
    fb_info.pAttachments = &m_fb_view.get();
    fb_info.width = m_size.x;
    fb_info.height = m_size.y;
    fb_info.layers = 1;
    m_framebuffer = dev->createFramebufferUnique(fb_info);

    m_fb_access_mask = vk::AccessFlags();
    m_fb_layout = vk::ImageLayout::eUndefined;

    return true;
}
