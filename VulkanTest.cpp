#include "pch.h"
#include "utils.h"
#include "app.h"
#include "rendertarget.h"

class DrawApp : public App
{
    RenderTarget rt;
    vk::UniqueSemaphore render_finished_sem;

public:
    virtual void on_init() override
    {
        rt.create(m_pd, m_dev, 512, 512);
        render_finished_sem = m_dev->createSemaphoreUnique(vk::SemaphoreCreateInfo());
    }

    virtual void on_render_frame(float dt) override
    {

        auto swapchain_sem = m_dev->createSemaphoreUnique(vk::SemaphoreCreateInfo());
        vk::ResultValue<uint32_t> swapchain_idx = m_dev->acquireNextImageKHR(*m_swapchain, UINT64_MAX, *swapchain_sem, nullptr);
        vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eTopOfPipe;
        auto submit_info = vk::SubmitInfo(1, &swapchain_sem.get(), &wait_stage, 1,
            &m_cmd[swapchain_idx.value].get(), 1, &render_finished_sem.get());
        auto fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
        m_main_queue.submit(submit_info, *fence);

        auto present_info = vk::PresentInfoKHR(1, &render_finished_sem.get(), 1, &m_swapchain.get(), &swapchain_idx.value);
        m_main_queue.presentKHR(present_info);
        m_dev->waitForFences(*fence, true, UINT64_MAX);
    }
};

int main()
{
    DrawApp app;
    app.init_vulkan();
    app.run_loop();
}
