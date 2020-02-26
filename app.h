#pragma once
#include "CmdRenderToScreen.h"

class App 
{
public:
    vk::PhysicalDevice m_pd;
    vk::UniqueInstance m_instance;
    vk::UniqueSurfaceKHR m_surf;
    vk::UniqueDevice m_dev;
    uint32_t m_family_idx;

    vk::Queue m_main_queue;
    vk::UniqueSwapchainKHR m_swapchain;
    vk::Extent2D m_swapchain_extent;
    vk::UniqueShaderModule m_vert_module;
    vk::UniqueShaderModule m_frag_module;
    vk::UniquePipeline m_pipeline;
    vk::UniqueRenderPass m_renderpass;
    vk::UniquePipelineLayout m_pipeline_layout;
    vk::UniqueDescriptorSetLayout m_descr_layout;
    vk::UniqueCommandPool m_cmd_pool;
    vk::UniqueDescriptorPool m_descr_pool;

    vk::UniqueSampler m_sampler;
    vk::UniqueImage m_tex_image;
    vk::UniqueImageView m_tex_view;
    vk::UniqueDeviceMemory m_tex_mem;

    std::vector<vk::Image> m_swapchain_images;
    std::vector<vk::UniqueImageView> m_swapchain_views;
    std::vector<vk::UniqueFramebuffer> m_framebuffers;
    std::vector<vk::UniqueDescriptorSet> m_descr;

    HWND m_wnd;

    bool init_vulkan();
    bool init_pipeline();
    std::tuple<vk::PhysicalDevice, vk::UniqueDevice, uint32_t> find_device();
    void resize(int width, int height);
    void create_swapchain();
    void create_commands();
    void run_loop();

    virtual void on_init() = 0;
    virtual void on_resize(int width, int height) = 0;
    virtual void on_render_frame(float dt) = 0;
};

