#include "pch.h"
#include "CmdRenderToScreen.h"

bool CmdRenderToScreen::create(const vk::UniqueDevice& m_dev, const vk::PhysicalDevice& m_pd, const vk::UniqueCommandPool& m_cmd_pool, 
    const vk::UniqueDescriptorPool& m_descr_pool, const vk::UniqueDescriptorSetLayout& m_descr_layout, 
    const vk::UniqueRenderPass& m_renderpass, const vk::UniqueFramebuffer& m_framebuffer, const vk::UniquePipeline& m_pipeline, 
    const vk::UniquePipelineLayout& m_pipeline_layout, const vk::UniqueSampler& m_sampler, 
    const vk::Extent2D m_swapchain_extent, const vk::UniqueImageView& m_tex_view, glm::vec3 clear_color)
{
    m_ubo.create(m_pd, m_dev);

    auto cmd_info = vk::CommandBufferAllocateInfo(*m_cmd_pool, vk::CommandBufferLevel::ePrimary, 1);
    m_cmd = std::move(m_dev->allocateCommandBuffersUnique(cmd_info).front());

    auto descr_info = vk::DescriptorSetAllocateInfo(*m_descr_pool, 1, &m_descr_layout.get());
    m_descr.release();
    m_descr = std::move(m_dev->allocateDescriptorSetsUnique(descr_info).front());

    auto descr_vert_buffer_info = vk::DescriptorBufferInfo(*m_ubo.m_buffer, 0, VK_WHOLE_SIZE);
    auto descr_image_info_fb = vk::DescriptorImageInfo(*m_sampler,
        *m_tex_view, vk::ImageLayout::eShaderReadOnlyOptimal);
    std::array<vk::WriteDescriptorSet, 2> descr_write = {
        vk::WriteDescriptorSet(*m_descr, 0, 0, 1,
            vk::DescriptorType::eUniformBuffer, nullptr, &descr_vert_buffer_info, nullptr),
        vk::WriteDescriptorSet(*m_descr, 1, 0, 1,
            vk::DescriptorType::eCombinedImageSampler, &descr_image_info_fb, nullptr, nullptr),
    };
    m_dev->updateDescriptorSets(descr_write, nullptr);

    vk::ClearValue clearColor(std::array<float, 4>{ clear_color.r, clear_color.g, clear_color.b, 1.f });
    auto begin_info = vk::RenderPassBeginInfo(*m_renderpass, *m_framebuffer,
        vk::Rect2D({ 0, 0 }, m_swapchain_extent), 1, &clearColor);

    auto pipeline_vp = vk::Viewport(0, 0, m_swapchain_extent.width, m_swapchain_extent.height, 0, 1);
    auto pipeline_vpscissor = vk::Rect2D({ 0, 0 }, m_swapchain_extent);

    m_cmd->begin(vk::CommandBufferBeginInfo());
    m_cmd->setViewport(0, pipeline_vp);
    m_cmd->setScissor(0, pipeline_vpscissor);

    m_cmd->beginRenderPass(begin_info, vk::SubpassContents::eInline);
    m_cmd->bindPipeline(vk::PipelineBindPoint::eGraphics, *m_pipeline);
    m_cmd->bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
        *m_pipeline_layout, 0, *m_descr, nullptr);
    m_cmd->draw(6, 1, 0, 0);

    m_cmd->endRenderPass();
    m_cmd->end();

    return true;
}
