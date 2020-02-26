#pragma once
#include "utils.h"

class CmdRenderToScreen
{
public:
    struct vert_ubo_t {
        glm::mat4 mvp;
    };

    glm::vec3 m_clear_color{ 0.3f, 0.0f, 0.0f };
    bool draw = true;
    vk::UniqueCommandBuffer m_cmd;
    vk::UniqueDescriptorSet m_descr;
    UBO<vert_ubo_t> m_ubo;

    bool create(const vk::UniqueDevice& m_dev, const vk::PhysicalDevice& m_pd, const vk::UniqueCommandPool& m_cmd_pool,
        const vk::UniqueDescriptorPool& m_descr_pool, const vk::UniqueDescriptorSetLayout& m_descr_layout, 
        const vk::UniqueRenderPass& m_renderpass, const vk::UniqueFramebuffer& m_framebuffer, const vk::UniquePipeline& m_pipeline, 
        const vk::UniquePipelineLayout& m_pipeline_layout, const vk::UniqueSampler& m_sampler, 
        const vk::Extent2D m_swapchain_extent, const vk::UniqueImageView& m_tex_view);
};
