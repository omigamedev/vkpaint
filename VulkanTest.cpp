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
    std::vector<CmdRenderToScreen> m_cmd_screen;

public:
    virtual void on_init() override
    {
        rt.create(m_pd, m_dev, 512, 512);
        m_tex.create(m_pd, m_dev, m_main_queue, m_cmd_pool, "brush.png");
        m_sampler = create_sampler(m_dev);
        render_finished_sem = m_dev->createSemaphoreUnique(vk::SemaphoreCreateInfo());
    }

    virtual void on_render_frame(float dt) override
    {

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

    virtual void on_resize(int width, int height) override
    {
        m_cmd_screen.resize(m_swapchain_images.size());

        for (size_t i = 0; i < m_swapchain_images.size(); i++)
        {
            m_cmd_screen[i].create(m_dev, m_pd, m_cmd_pool, m_descr[i], m_renderpass, m_framebuffers[i],
                m_pipeline, m_pipeline_layout, m_sampler, m_swapchain_extent, m_tex.m_view);
        }
    }

};

int main()
{
    DrawApp app;
    app.init_vulkan();
    app.run_loop();
}
