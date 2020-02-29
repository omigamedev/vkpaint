#pragma once
#include "utils.h"

class CmdRenderStroke
{
public:
    struct vert_ubo_t {
        glm::mat4 mvp;
    };
    struct frag_ubo_t {
        glm::vec3 col;
    };

    vk::UniqueCommandBuffer m_cmd;
    vk::UniqueDescriptorSet m_descr;
    UBO<vert_ubo_t> m_vert_ubo;
    UBO<frag_ubo_t> m_frag_ubo;
    bool m_cleared = false;

    bool create(const vk::UniqueDevice& m_dev, const vk::PhysicalDevice& m_pd, const vk::UniqueCommandPool& m_cmd_pool,
        const vk::UniqueDescriptorPool& m_descr_pool, const vk::UniqueDescriptorSetLayout& m_descr_layout, 
        const vk::UniqueRenderPass& m_renderpass, const vk::UniqueFramebuffer& m_framebuffer, const vk::UniquePipeline& m_pipeline, 
        const vk::UniquePipelineLayout& m_pipeline_layout, const vk::UniqueSampler& m_sampler, 
        const vk::Extent2D m_swapchain_extent, const vk::UniqueImage& m_fb_img, const vk::UniqueImageView& m_fb_view, 
        const vk::UniqueImageView& m_brush_view, glm::vec3 clear_color = glm::vec3(1, 0, 0));
};
