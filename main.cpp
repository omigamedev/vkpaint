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
    vk::UniqueSampler m_sampler_linear;
    vk::UniqueSampler m_sampler_nearest;
    std::vector<CmdRenderToScreen> m_cmd_screen;
    float m_zoom = 1.f;
    glm::vec2 m_pan = { 0, 0 };

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
            rt.m_framebuffer, rt.m_pipeline, rt.m_layout, m_sampler_linear, vk::Extent2D(rt.m_size.x, rt.m_size.y),
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
            if ((int)m_samples > 1)
                save_image(rt.m_resolved_img, rt.m_size, "out.jpg", false);
            else
                save_image(rt.m_fb_img, rt.m_size, "out.hdr", true);
        }
        else if (keycode == 'C')
        {
            clear_rt();
        }
        else if (keycode == 'R')
        {
            m_zoom = 1.f;
            m_pan = { 0, 0 };
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
                std::string title = fmt::format("Vulkan {} - {} fps - {} stroke/sec - res {}x{}{}",
                    m_device_name, frames, m_strokes_count,
                    rt.m_size.x, rt.m_size.y,
                    (int)m_samples > 1 ? fmt::format(" - MSAA {}x", (int)m_samples) : "");
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
                rt.m_framebuffer, rt.m_pipeline, rt.m_layout, m_sampler_linear, vk::Extent2D(rt.m_size.x, rt.m_size.y),
                rt.m_fb_img, rt.m_fb_view, m_tex.m_view, { 0, 1, 0 });
        }

        std::cout << "canvas ready\n";

        while (m_running)
        {
            std::vector<StrokeSample> samples;
            {
                std::unique_lock lock(m_stroke_mutex);
                m_stroke_cv.wait(lock, [&] {
                    if (m_running && m_stroke_samples.empty()) return false; // keep waiting 
                    else return true;
                });
                samples = std::move(m_stroke_samples);
            }

            if (!m_running)
                break;

            int buf_size = m_cmd_strokes.size() - 1;
            int n = std::ceilf((float)samples.size() / buf_size);
            for (int blk = 0; blk < n; blk++)
            {
                int i = 0;
                int offset = blk * m_cmd_strokes.size();
                int samples_count = std::min<int>(buf_size, samples.size() - offset);
                for (; i < samples_count; i++)
                {
                    float x = samples[offset + i].cur.x;
                    float y = -samples[offset + i].cur.y;

                    m_cmd_strokes[i].m_frag_ubo.m_value.col = glm::vec3(0, 0, 0);
                    m_cmd_strokes[i].m_frag_ubo.m_value.pressure = 1.f;
                    m_cmd_strokes[i].m_frag_ubo.update(m_dev);

                    m_cmd_strokes[i].m_vert_ubo.m_value.mvp = glm::translate(glm::vec3(x, y, 0))
                        * glm::scale(glm::vec3(0.01f * samples[offset + i].pressure));
                    m_cmd_strokes[i].m_vert_ubo.update(m_dev);
                    m_strokes_count++;

                    cmd_strokes_cmd[i] = *m_cmd_strokes[i].m_cmd;
                }
                offset += samples_count;

                vk::UniqueFence strokes_fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
                if (m_samples == vk::SampleCountFlagBits::e1 || (blk < n - 1))
                {
                    vk::SubmitInfo si;
                    si.commandBufferCount = samples_count;
                    si.pCommandBuffers = cmd_strokes_cmd.data();
                    m_main_queue_mutex.lock();
                    m_main_queue.submit(si, *strokes_fence);
                    m_main_queue_mutex.unlock();
                    m_dev->waitForFences(*strokes_fence, true, UINT64_MAX);
                }
                else
                {
                    cmd_strokes_cmd[i] = *rt.cmd_resolve;

                    vk::SubmitInfo si;
                    si.commandBufferCount = samples_count + 1;
                    si.pCommandBuffers = cmd_strokes_cmd.data();
                    m_main_queue_mutex.lock();
                    m_main_queue.submit(si, *strokes_fence);
                    m_main_queue_mutex.unlock();
                }
                m_dev->waitForFences(*strokes_fence, true, UINT64_MAX);
            }

            //std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    virtual void on_init() override
    {
        rt.create(m_pd, m_dev, 4096, 4096, m_samples);
        if ((int)m_samples > 1)
            rt.create_resolver(m_dev, m_cmd_pool, m_main_queue);
        m_tex.create(m_pd, m_dev, m_main_queue, m_cmd_pool, "brush.png");
        m_sampler_linear = create_sampler(m_dev, vk::Filter::eLinear);
        m_sampler_nearest = create_sampler(m_dev, vk::Filter::eNearest);
        render_finished_sem = m_dev->createSemaphoreUnique(vk::SemaphoreCreateInfo());
        clear_rt();

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

        for (size_t i = 0; i < m_swapchain_images.size(); i++)
        {
            float aspect = (float)m_swapchain_extent.width / (float)m_swapchain_extent.height;
            m_cmd_screen[i].m_ubo.m_value.mvp = glm::ortho<float>(-aspect, aspect, -1, 1) * glm::translate(glm::vec3(m_pan, 0)) 
                * glm::scale(glm::vec3(m_zoom));
            m_cmd_screen[i].m_ubo.update(m_dev);
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
                m_framebuffers[i], m_pipeline, m_pipeline_layout, m_sampler_linear, m_swapchain_extent, 
                (int)m_samples > 1 ? rt.m_resolved_view : rt.m_fb_view, glm::vec3(0.3f));
            m_cmd_screen[i].m_ubo.m_value.mvp = glm::identity<glm::mat4>();
            m_cmd_screen[i].m_ubo.update(m_dev);
        }
    }

    glm::ivec2 m_cur_pos;
    bool m_dragL = false;
    bool m_dragR = false;
    glm::mat4 m_mat_start;
    glm::vec2 m_pan_start;
    glm::vec2 m_pan_value;

    virtual void on_mouse_move(glm::ivec2 pos, float pressure) override
    {
        glm::vec2 sz = { m_swapchain_extent.width, m_swapchain_extent.height };
        auto m = glm::inverse(m_cmd_screen[0].m_ubo.m_value.mvp);
        if (m_dragL)
        {
            //m_stroke_samples.emplace_back((glm::vec2(pos) / sz) * 2.f - 1.f);
            int dist = glm::ceil(glm::distance(glm::vec2(m_cur_pos), glm::vec2(pos))) * 10;
            if (dist > 0)
            {
                std::lock_guard lock(m_stroke_mutex);
                for (int i = 0; i < dist; i++)
                {
                    glm::vec2 p = glm::lerp(glm::vec2(m_cur_pos), glm::vec2(pos), (float)i / dist);
                    p = (p / sz) * 2.f - 1.f;
                    p = m * glm::vec4(p, 0, 1);
                    m_stroke_samples.emplace_back(p, pressure);
                }
            }
            m_stroke_cv.notify_one();
        }
        if (m_dragR)
        {
            glm::vec2 pos_n = m_mat_start * glm::vec4((glm::vec2(pos) / sz) * 2.f - 1.f, 0.f, 1.f);
            m_pan = m_pan_value + glm::vec2(pos_n - m_pan_start) * m_zoom;
        }
        m_cur_pos = pos;
    }

    virtual void on_mouse_down(int button, glm::ivec2 pos, float pressure) override
    {
        m_cur_pos = pos;
        if (button == 0)
        {
            m_dragL = true;
        }
        else if (button == 1)
        {
            glm::vec2 sz = { m_swapchain_extent.width, m_swapchain_extent.height };
            m_mat_start = glm::inverse(m_cmd_screen[0].m_ubo.m_value.mvp);
            m_pan_start = m_mat_start * glm::vec4((glm::vec2(pos) / sz) * 2.f - 1.f, 0.f, 1.f);
            m_pan_value = m_pan;
            m_dragR = true;
        }
    }

    virtual void on_mouse_up(int button, glm::ivec2 pos) override
    {
        if (button == 0)
        {
            m_dragL = false;
        }
        else if (button == 1)
        {
            m_dragR = false;
        }
    }

    virtual void on_terminate() override
    {
        m_stroke_cv.notify_one();
        if (m_canvas_render_thread.joinable())
            m_canvas_render_thread.join();
        if (m_main_render_thread.joinable())
            m_main_render_thread.join();
    }

    virtual void on_mouse_wheel(glm::ivec2 pos, float delta) override
    {
        m_zoom += m_zoom * 0.1f * delta;
    }

};

int main()
{
    auto app = std::make_unique<DrawApp>();
    app->init_vulkan();
    app->run_loop();
}
