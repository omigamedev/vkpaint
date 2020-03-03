#include "pch.h"
#include "utils.h"
#include "app.h"
#include "rendertarget.h"
#include "texture.h"
#include "CmdRenderStroke.h"
#include "debug_message.h"

class DrawApp : public App
{
    RenderTarget rt;
    Texture m_tex;
    vk::UniqueSemaphore render_finished_sem;
    vk::UniqueSampler m_sampler;
    std::vector<CmdRenderToScreen> m_cmd_screen;

    std::thread m_render_thread;

public:

    void clear_rt()
    {
        CmdRenderStroke m_cmd_stroke_clear;
        m_cmd_stroke_clear.create(m_dev, m_pd, m_cmd_pool, m_descr_pool, rt.m_descr_layout, rt.m_renderpass,
            rt.m_framebuffer, rt.m_pipeline, rt.m_layout, m_sampler, vk::Extent2D(rt.m_size.x, rt.m_size.y),
            rt.m_fb_img, rt.m_fb_view, m_tex.m_view, { 0, 1, 0 });

        rt.to_layout(m_dev, m_cmd_pool, m_main_queue, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::PipelineStageFlagBits::eBottomOfPipe, vk::PipelineStageFlagBits::eFragmentShader);

        vk::SubmitInfo si;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &m_cmd_stroke_clear.m_cmd.get();
        vk::UniqueFence submit_fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
        m_main_queue.submit(si, *submit_fence);
        m_dev->waitForFences(*submit_fence, true, UINT64_MAX);
    }

    virtual void on_keyup(int keycode) override
    {
        if (keycode == VK_SPACE)
        {
            save_image(rt.m_fb_img, rt.m_size, "out.hdr");
        }
    }

    void render_thread()
    {
        float theta = 0;

        auto cmd_pool_info = vk::CommandPoolCreateInfo({}, m_family_idx);
        vk::UniqueCommandPool cmd_pool = m_dev->createCommandPoolUnique(cmd_pool_info);

        std::array<vk::DescriptorPoolSize, 2> descr_pool_size = {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1000),
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1000),
        };
        auto descr_pool_info = vk::DescriptorPoolCreateInfo(
            vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            2000, descr_pool_size.size(), descr_pool_size.data());
        vk::UniqueDescriptorPool descr_pool = m_dev->createDescriptorPoolUnique(descr_pool_info);
        
        std::vector<CmdRenderStroke> m_cmd_strokes(100);
        std::vector<vk::CommandBuffer> cmd_strokes_cmd(m_cmd_strokes.size());
        for (int i = 0; i < m_cmd_strokes.size(); i++)
        {
            m_cmd_strokes[i].m_cleared = true;
            m_cmd_strokes[i].create(m_dev, m_pd, cmd_pool, descr_pool, rt.m_descr_layout, rt.m_renderpass,
                rt.m_framebuffer, rt.m_pipeline, rt.m_layout, m_sampler, vk::Extent2D(rt.m_size.x, rt.m_size.y),
                rt.m_fb_img, rt.m_fb_view, m_tex.m_view, { 0, 1, 0 });
            cmd_strokes_cmd[i] = *m_cmd_strokes[i].m_cmd;
        }

        while (m_running)
        {
            for (int i = 0; i < m_cmd_strokes.size(); i++)
            {
                theta += 0.01 / m_cmd_strokes.size();
                float x = glm::sin(theta);
                float y = glm::cos(theta);

                m_cmd_strokes[i].m_frag_ubo.m_value.col = glm::vec3(glm::abs(y), 0, glm::abs(x));
                m_cmd_strokes[i].m_frag_ubo.update(m_dev);

                m_cmd_strokes[i].m_vert_ubo.m_value.mvp = glm::scale(glm::vec3(0.75f))
                    * glm::translate(glm::vec3(x, y, 0))
                    * glm::scale(glm::vec3(0.25f));
                m_cmd_strokes[i].m_vert_ubo.update(m_dev);
                m_strokes_count++;
            }

            vk::SubmitInfo si;
            si.commandBufferCount = cmd_strokes_cmd.size();
            si.pCommandBuffers = cmd_strokes_cmd.data();
            vk::UniqueFence strokes_fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
            m_main_queue_mutex.lock();
            m_main_queue.submit(si, *strokes_fence);
            m_main_queue_mutex.unlock();
            m_dev->waitForFences(*strokes_fence, true, UINT64_MAX);

            //std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    virtual void on_init() override
    {
        rt.create(m_pd, m_dev, 1024, 1024);
        m_tex.create(m_pd, m_dev, m_main_queue, m_cmd_pool, "brush.png");
        m_sampler = create_sampler(m_dev);
        render_finished_sem = m_dev->createSemaphoreUnique(vk::SemaphoreCreateInfo());
        clear_rt();

        // setup debug marke ext
        debug_name(rt.m_fb_img, "Render Target Color Image");
        debug_name(rt.m_fb_view, "Render Target Color View");

        m_render_thread = std::thread(&DrawApp::render_thread, this);
    }

    virtual void on_render_frame(float dt) override
    {
        auto swapchain_sem = m_dev->createSemaphoreUnique(vk::SemaphoreCreateInfo());
        vk::ResultValue<uint32_t> swapchain_idx = m_dev->acquireNextImageKHR(*m_swapchain, UINT64_MAX, *swapchain_sem, nullptr);
        vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        auto submit_info = vk::SubmitInfo(1, &swapchain_sem.get(), &wait_stage, 1,
            &m_cmd_screen[swapchain_idx.value].m_cmd.get(), 1, &render_finished_sem.get());
        auto fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
        auto present_info = vk::PresentInfoKHR(1, &render_finished_sem.get(), 1, &m_swapchain.get(), &swapchain_idx.value);

        m_main_queue_mutex.lock();
        m_main_queue.submit(submit_info, *fence);
        m_main_queue_mutex.unlock();
        m_dev->waitForFences(*fence, true, UINT64_MAX);

        m_main_queue_mutex.lock();
        m_main_queue.presentKHR(present_info);
        m_main_queue_mutex.unlock();
    }

    virtual void on_resize() override
    {
        App::on_resize();
        m_cmd_screen.clear();
        m_cmd_screen.resize(m_swapchain_images.size());
        for (size_t i = 0; i < m_swapchain_images.size(); i++)
        {
            m_cmd_screen[i].create(m_dev, m_pd, m_cmd_pool, m_descr_pool, m_descr_layout, m_renderpass, 
                m_framebuffers[i], m_pipeline, m_pipeline_layout, m_sampler, m_swapchain_extent, rt.m_fb_view);
            m_cmd_screen[i].m_ubo.m_value.mvp = glm::identity<glm::mat4>();
            m_cmd_screen[i].m_ubo.update(m_dev);
        }
    }

    glm::ivec2 m_cur_pos;

    virtual void on_mouse_move(glm::ivec2 pos) override
    {
        int dist = glm::ceil(glm::distance(glm::vec2(m_cur_pos), glm::vec2(pos)));
        if (dist > 0)
        {
            for (int i = 0; i < dist; i++)
            {

            }
        }
        m_cur_pos = pos;
    }

    virtual void on_mouse_down(glm::ivec2 pos) override
    {

    }

    virtual void on_mouse_up(glm::ivec2 pos) override
    {

    }

    virtual void on_terminate() override
    {
        if (m_render_thread.joinable())
            m_render_thread.join();
    }
};

int main()
{
    auto app = std::make_unique<DrawApp>();
    app->init_vulkan();
    app->run_loop();
}
