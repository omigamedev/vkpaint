#include "pch.h"
#include "utils.h"
#include "app.h"
#include "rendertarget.h"
#include "texture.h"

class DrawApp : public App
{
    RenderTarget rt;
    Texture m_tex;
    vk::UniqueSemaphore render_finished_sem;
    vk::UniqueSampler m_sampler;
    CmdRenderToScreen m_cmd_filler;
    std::vector<CmdRenderToScreen> m_cmd_screen;

public:
    virtual void on_init() override
    {
        rt.create(m_pd, m_dev, 512, 512);
        m_tex.create(m_pd, m_dev, m_main_queue, m_cmd_pool, "brush.png");
        m_sampler = create_sampler(m_dev);
        render_finished_sem = m_dev->createSemaphoreUnique(vk::SemaphoreCreateInfo());

        m_cmd_filler.m_clear_color = { 0, 1, 0 };
        //m_cmd_filler.draw = false;
        m_cmd_filler.create(m_dev, m_pd, m_cmd_pool, m_descr_pool, rt.m_descr_layout, rt.m_renderpass,
            rt.m_framebuffer, rt.m_pipeline, rt.m_layout, m_sampler, vk::Extent2D(rt.m_size.x, rt.m_size.y), m_tex.m_view);
        m_cmd_filler.m_ubo.m_value.mvp = glm::scale(glm::vec3(0.5f));
        m_cmd_filler.m_ubo.update(m_dev);

    }

    virtual void on_render_frame(float dt) override
    {
        vk::SubmitInfo si;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &m_cmd_filler.m_cmd.get();
        vk::UniqueFence submit_fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
        m_main_queue.submit(si, *submit_fence);
        m_dev->waitForFences(*submit_fence, true, UINT64_MAX);

        auto swapchain_sem = m_dev->createSemaphoreUnique(vk::SemaphoreCreateInfo());
        vk::ResultValue<uint32_t> swapchain_idx = m_dev->acquireNextImageKHR(*m_swapchain, UINT64_MAX, *swapchain_sem, nullptr);
        vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        auto submit_info = vk::SubmitInfo(1, &swapchain_sem.get(), &wait_stage, 1,
            &m_cmd_screen[swapchain_idx.value].m_cmd.get(), 1, &render_finished_sem.get());
        auto fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
        m_main_queue.submit(submit_info, *fence);

        auto present_info = vk::PresentInfoKHR(1, &render_finished_sem.get(), 1, &m_swapchain.get(), &swapchain_idx.value);
        m_main_queue.presentKHR(present_info);
        m_dev->waitForFences(*fence, true, UINT64_MAX);
    }

    virtual void on_resize() override
    {
        m_cmd_screen.resize(m_swapchain_images.size());

        for (size_t i = 0; i < m_swapchain_images.size(); i++)
        {
            m_cmd_screen[i].create(m_dev, m_pd, m_cmd_pool, m_descr_pool, m_descr_layout, m_renderpass, 
                m_framebuffers[i], m_pipeline, m_pipeline_layout, m_sampler, m_swapchain_extent, rt.m_fb_view);
            m_cmd_screen[i].m_ubo.m_value.mvp = glm::scale(glm::vec3(0.5f));
            m_cmd_screen[i].m_ubo.update(m_dev);
            //m_cmd_screen[i].draw = false;
        }
    }

};

int main()
{
    DrawApp app;
    app.init_vulkan();
    app.run_loop();
}
