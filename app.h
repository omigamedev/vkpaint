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

    std::vector<vk::Image> m_swapchain_images;
    std::vector<vk::UniqueImageView> m_swapchain_views;
    std::vector<vk::UniqueFramebuffer> m_framebuffers;

    HWND m_wnd;
    std::string m_device_name;
    uint32_t m_strokes_count = 0;
    bool swapchain_needs_recreation = false;
    glm::ivec2 cur_pos;
    glm::ivec2 wnd_size;

    App() { I = this; }

    bool init_vulkan();
    bool init_pipeline();
    std::tuple<vk::PhysicalDevice, vk::UniqueDevice, uint32_t> find_device();
    void resize();
    void create_swapchain();
    void create_window();
    void run_loop();

    static App* I;
    static LRESULT CALLBACK wnd_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    virtual void on_init() = 0;
    virtual void on_resize() = 0;
    virtual void on_render_frame(float dt) = 0;
    virtual void on_keyup(int keycode) = 0;
};
