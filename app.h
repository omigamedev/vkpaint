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

    vk::UniqueSwapchainKHR m_swapchain;
    vk::Extent2D m_swapchain_extent;
    vk::UniqueShaderModule m_vert_module;
    vk::UniqueShaderModule m_frag_module;
    vk::UniquePipeline m_pipeline;
    vk::UniqueRenderPass m_renderpass;
    vk::UniquePipelineLayout m_pipeline_layout;
    vk::UniqueDescriptorSetLayout m_descr_layout;

    std::mutex m_main_queue_mutex;
    vk::Queue m_main_queue;

    vk::UniqueCommandPool m_cmd_pool;
    vk::UniqueDescriptorPool m_descr_pool;

    std::vector<vk::Image> m_swapchain_images;
    std::vector<vk::UniqueImageView> m_swapchain_views;
    std::vector<vk::UniqueFramebuffer> m_framebuffers;
    std::mutex m_swapchain_mutex;

    HWND m_wnd = NULL;
    std::string m_device_name;
    uint32_t m_strokes_count = 0;
    bool m_running = true;

    App() { I = this; }

    bool init_vulkan();
    bool init_pipeline();
    std::tuple<vk::PhysicalDevice, vk::UniqueDevice, uint32_t> find_device();
    void create_swapchain();
    void create_window();
    void save_image(const vk::UniqueImage& img, const glm::ivec2 sz, const std::filesystem::path& path);
    void run_loop();

    static App* I;
    static LRESULT CALLBACK wnd_proc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    virtual void on_resize() = 0;
    virtual void on_init() = 0;
    virtual void on_terminate() = 0;
    virtual void on_keyup(int keycode) = 0;
    virtual void on_mouse_move(glm::ivec2 pos, float pressure) = 0;
    virtual void on_mouse_down(glm::ivec2 pos, float pressure) = 0;
    virtual void on_mouse_up(glm::ivec2 pos) = 0;
};
