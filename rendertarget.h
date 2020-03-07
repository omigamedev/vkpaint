#pragma once

class RenderTarget
{
    bool create_framebuffer(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev);
public:
    vk::UniqueDescriptorSetLayout m_descr_layout;
    vk::UniquePipelineLayout m_layout;
    vk::UniquePipeline m_pipeline;

    vk::UniqueShaderModule m_shader_vert;
    vk::UniqueShaderModule m_shader_frag;

    vk::UniqueImage m_fb_img;
    vk::UniqueImage m_resolved_img;
    vk::UniqueImageView m_fb_view;
    vk::UniqueImageView m_resolved_view;
    vk::UniqueDeviceMemory m_fb_mem;
    vk::UniqueDeviceMemory m_resolved_mem;
    vk::UniqueRenderPass m_renderpass;
    vk::UniqueFramebuffer m_framebuffer;
    vk::AccessFlags m_fb_access_mask;
    vk::ImageLayout m_fb_layout;

    vk::UniqueCommandBuffer cmd_resolve;

    glm::ivec2 m_size;
    vk::SampleCountFlagBits m_samples;
    vk::Format m_format;

    bool create(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev, int width, int height, vk::SampleCountFlagBits samples, vk::Format format);
    bool create_resolver(const vk::UniqueDevice& m_dev, const vk::UniqueCommandPool& cmd_pool, const vk::Queue& cmd_queue);
    void to_layout(const vk::UniqueDevice& dev, const vk::UniqueCommandPool& cmd_pool, const vk::Queue& q,
        vk::AccessFlags access_mask, vk::ImageLayout layout, vk::PipelineStageFlags src_stage, vk::PipelineStageFlags dst_stage);
    void resolve(const vk::UniqueDevice& m_dev, const vk::UniqueCommandPool& cmd_pool, const vk::Queue& cmd_queue);
};
