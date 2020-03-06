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

    struct StrokeSample
    {
        glm::vec2 cur;
        float pressure;
        StrokeSample(glm::vec2 pos, float pressure) : cur(pos), pressure(pressure) {}
    };
    std::mutex m_stroke_mutex;
    std::condition_variable m_stroke_cv;
    std::vector<StrokeSample> m_stroke_samples;

    std::thread m_canvas_render_thread;
    std::thread m_main_render_thread;
public:

    void clear_rt()
    {
        CmdRenderStroke m_cmd_stroke_clear;
        m_cmd_stroke_clear.create(m_dev, m_pd, m_cmd_pool, m_descr_pool, rt.m_descr_layout, rt.m_renderpass,
            rt.m_framebuffer, rt.m_pipeline, rt.m_layout, m_sampler, vk::Extent2D(rt.m_size.x, rt.m_size.y),
            rt.m_fb_img, rt.m_fb_view, m_tex.m_view, { 1, 1, 1 });

        rt.to_layout(m_dev, m_cmd_pool, m_main_queue, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::PipelineStageFlagBits::eBottomOfPipe, vk::PipelineStageFlagBits::eFragmentShader);

        vk::SubmitInfo si;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &m_cmd_stroke_clear.m_cmd.get();
        vk::UniqueFence submit_fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
        m_main_queue_mutex.lock();
        m_main_queue.submit(si, *submit_fence);
        m_main_queue_mutex.unlock();
        m_dev->waitForFences(*submit_fence, true, UINT64_MAX);
    }

    virtual void on_keyup(int keycode) override
    {
        if (keycode == VK_SPACE)
        {
            save_image(rt.m_fb_img, rt.m_size, "out.hdr");
        }
        else if (keycode == 'C')
        {
            clear_rt();
        }
    }

    void main_render_thread()
    {
        auto timer_start = std::chrono::high_resolution_clock::now();
        uint32_t frames = 0;
        float timer_fps = 0;
        while (m_running)
        {
            auto timer_stop = std::chrono::high_resolution_clock::now();
            auto timer_diff = std::chrono::duration<float>(timer_stop - timer_start);
            timer_start = timer_stop;
            float dt = timer_diff.count();
            if (render_frame(dt))
                frames++;

            timer_fps += dt;
            float timer_fps_sec;
            float timer_fps_dec = std::modf(timer_fps, &timer_fps_sec);
            if (timer_fps_sec >= 1.f)
            {
                timer_fps = timer_fps_dec;
                std::string title = fmt::format("Vulkan {} - {} fps - {} stroke/sec",
                    m_device_name, frames, m_strokes_count);
                SetWindowTextA(m_wnd, title.c_str());
                frames = 0;
                m_strokes_count = 0;
            }
        }
    }

    void canvas_render_thread()
    {
        auto cmd_pool_info = vk::CommandPoolCreateInfo({}, m_family_idx);
        vk::UniqueCommandPool cmd_pool = m_dev->createCommandPoolUnique(cmd_pool_info);

        const size_t n = 1000;
        std::array<vk::DescriptorPoolSize, 2> descr_pool_size = {
            vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, n),
            vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, n),
        };
        auto descr_pool_info = vk::DescriptorPoolCreateInfo(
            vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            n * 2, descr_pool_size.size(), descr_pool_size.data());
        vk::UniqueDescriptorPool descr_pool = m_dev->createDescriptorPoolUnique(descr_pool_info);
        
        std::vector<CmdRenderStroke> m_cmd_strokes(n);
        std::vector<vk::CommandBuffer> cmd_strokes_cmd(m_cmd_strokes.size());
        for (int i = 0; i < m_cmd_strokes.size(); i++)
        {
            m_cmd_strokes[i].m_cleared = true;
            m_cmd_strokes[i].create(m_dev, m_pd, cmd_pool, descr_pool, rt.m_descr_layout, rt.m_renderpass,
                rt.m_framebuffer, rt.m_pipeline, rt.m_layout, m_sampler, vk::Extent2D(rt.m_size.x, rt.m_size.y),
                rt.m_fb_img, rt.m_fb_view, m_tex.m_view, { 0, 1, 0 });
            cmd_strokes_cmd[i] = *m_cmd_strokes[i].m_cmd;
        }

        std::cout << "canvas ready\n";

        while (m_running)
        {
            std::unique_lock lock(m_stroke_mutex);
            m_stroke_cv.wait(lock, [&] {
                if (m_running && m_stroke_samples.empty()) return false; // keep waiting 
                else return true;
            });

            if (!m_running)
                break;

            auto samples = std::move(m_stroke_samples);
            lock.unlock();

            int samples_count = std::min(samples.size(), m_cmd_strokes.size());
            for (int i = 0; i < samples_count; i++)
            {
                float x = samples[i].cur.x;
                float y = -samples[i].cur.y;

                m_cmd_strokes[i].m_frag_ubo.m_value.col = glm::vec3(0, 0, 0);
                m_cmd_strokes[i].m_frag_ubo.m_value.pressure = 1.f;// samples[i].pressure;
                m_cmd_strokes[i].m_frag_ubo.update(m_dev);

                m_cmd_strokes[i].m_vert_ubo.m_value.mvp = glm::translate(glm::vec3(x, y, 0))
                    * glm::scale(glm::vec3(0.01f * samples[i].pressure));
                m_cmd_strokes[i].m_vert_ubo.update(m_dev);
                m_strokes_count++;
            }

            vk::SubmitInfo si;
            si.commandBufferCount = samples_count;
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

        m_canvas_render_thread = std::thread(&DrawApp::canvas_render_thread, this);
        m_main_render_thread = std::thread(&DrawApp::main_render_thread, this);
    }

    virtual bool render_frame(float dt)
    {
        static float timer = 0;

        timer += dt;
        const float period = 1.f / 60.f;
        if (timer < period)
        {
            std::this_thread::sleep_for(std::chrono::duration<float>(period - timer));
            return false;
        }

        std::lock_guard lock(m_swapchain_mutex);

        auto swapchain_sem = m_dev->createSemaphoreUnique(vk::SemaphoreCreateInfo());
        vk::ResultValue<uint32_t> swapchain_idx = m_dev->acquireNextImageKHR(*m_swapchain, UINT64_MAX, *swapchain_sem, nullptr);
        vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        auto submit_info = vk::SubmitInfo(1, &swapchain_sem.get(), &wait_stage, 1,
            &m_cmd_screen[swapchain_idx.value].m_cmd.get(), 1, &render_finished_sem.get());
        auto fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
        auto present_info = vk::PresentInfoKHR(1, &render_finished_sem.get(), 1, &m_swapchain.get(), &swapchain_idx.value);

        auto present_start = std::chrono::high_resolution_clock::now();
        m_main_queue_mutex.lock();
        m_main_queue.submit(submit_info, *fence);
        m_dev->waitForFences(*fence, true, UINT64_MAX);
        try
        {
            m_main_queue.presentKHR(present_info);
        }
        catch (const vk::SystemError& err)
        {
            std::cout << err.what() << "\n";
        }
        m_main_queue_mutex.unlock();

        //auto timer_diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - present_start);
        //int64_t present_dt = timer_diff.count();
        //if (present_dt > 0)
        //    std::cout << fmt::format("present time: {}ms\n", present_dt);

        timer = 0;
        
        return true;
    }

    virtual void on_resize() override
    {
        create_swapchain();
        std::lock_guard lock(m_swapchain_mutex);
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
    bool m_drag = false;

    virtual void on_mouse_move(glm::ivec2 pos, float pressure) override
    {
        if (m_drag)
        {
            glm::vec2 sz = { m_swapchain_extent.width, m_swapchain_extent.height };
            //m_stroke_samples.emplace_back((glm::vec2(pos) / sz) * 2.f - 1.f);
            int dist = glm::ceil(glm::distance(glm::vec2(m_cur_pos), glm::vec2(pos))) * 10;
            if (dist > 0)
            {
                std::lock_guard lock(m_stroke_mutex);
                for (int i = 0; i < dist; i++)
                {
                    glm::vec2 p = glm::lerp(glm::vec2(m_cur_pos), glm::vec2(pos), (float)i / dist);
                    m_stroke_samples.emplace_back((p / sz) * 2.f - 1.f, pressure);
                }
            }
            m_cur_pos = pos;
            m_stroke_cv.notify_one();
        }
    }

    virtual void on_mouse_down(glm::ivec2 pos, float pressure) override
    {
        m_cur_pos = pos;
        m_drag = true;
    }

    virtual void on_mouse_up(glm::ivec2 pos) override
    {
        m_drag = false;
    }

    virtual void on_terminate() override
    {
        m_stroke_cv.notify_one();
        if (m_canvas_render_thread.joinable())
            m_canvas_render_thread.join();
        if (m_main_render_thread.joinable())
            m_main_render_thread.join();
    }
};

int main()
{
    auto app = std::make_unique<DrawApp>();
    app->init_vulkan();
    app->run_loop();
}
