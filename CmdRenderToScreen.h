#pragma once
#include "utils.h"

class CmdRenderToScreen
{
public:
    struct vert_ubo_t {
        glm::mat4 mvp;
    };

    vk::UniqueCommandBuffer m_cmd;
    UBO<vert_ubo_t> m_ubo;

    bool create(const vk::UniqueDevice& m_dev, const vk::PhysicalDevice& m_pd, const vk::UniqueCommandPool& m_cmd_pool,
        const vk::UniqueDescriptorSet& m_descr, const vk::UniqueRenderPass& m_renderpass, const vk::UniqueFramebuffer& m_framebuffer,
        const vk::UniquePipeline& m_pipeline, const vk::UniquePipelineLayout& m_pipeline_layout, const vk::UniqueSampler& m_sampler, 
        const vk::Extent2D m_swapchain_extent, const vk::UniqueImageView& m_tex_view);
};
