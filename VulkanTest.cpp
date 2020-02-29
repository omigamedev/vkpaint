#include "pch.h"
#include "utils.h"
#include "app.h"
#include "rendertarget.h"
#include "texture.h"
#include "CmdRenderStroke.h"

class DrawApp : public App
{
    RenderTarget rt;
    Texture m_tex;
    vk::UniqueSemaphore render_finished_sem;
    vk::UniqueSampler m_sampler;
    std::vector<CmdRenderStroke> m_cmd_strokes;
    std::vector<vk::CommandBuffer> m_cmd_strokes_cmd;
    std::vector<CmdRenderToScreen> m_cmd_screen;

public:
    void save_image(const vk::UniqueImage& img, const glm::ivec2 sz, const std::filesystem::path& path)
    {
        vk::DeviceSize pix_sz = sz.x * sz.y * 4 * sizeof(float);

        // staging buffer
        vk::BufferCreateInfo buf_info;
        buf_info.size = pix_sz;
        buf_info.usage = vk::BufferUsageFlagBits::eTransferDst;
        vk::UniqueBuffer buf = m_dev->createBufferUnique(buf_info);
        vk::MemoryRequirements buf_req = m_dev->getBufferMemoryRequirements(*buf);
        uint32_t buf_mem_idx = find_memory(m_pd, buf_req, vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent);
        vk::UniqueDeviceMemory buf_mem = m_dev->allocateMemoryUnique({ buf_req.size, buf_mem_idx });
        m_dev->bindBufferMemory(*buf, *buf_mem, 0);

        vk::UniqueCommandBuffer cmd = std::move(m_dev->allocateCommandBuffersUnique(
            { *m_cmd_pool, vk::CommandBufferLevel::ePrimary, 1 }).front());
        cmd->begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
        {
            vk::ImageMemoryBarrier imb;
            imb.srcAccessMask = vk::AccessFlags();
            imb.dstAccessMask = vk::AccessFlagBits::eTransferRead;
            imb.oldLayout = vk::ImageLayout::eUndefined;
            imb.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            imb.srcQueueFamilyIndex = imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imb.image = *img;
            imb.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 0, nullptr, 1, &imb);

            vk::BufferImageCopy bic;
            bic.bufferOffset = 0;
            bic.bufferRowLength = sz.x;
            bic.bufferImageHeight = sz.y;
            bic.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            bic.imageOffset = vk::Offset3D();
            bic.imageExtent = vk::Extent3D(sz.x, sz.y, 1);
            //cmd->copyBufferToImage(*buf, *img, vk::ImageLayout::eTransferDstOptimal, bic);
            cmd->copyImageToBuffer(*img, vk::ImageLayout::eTransferSrcOptimal, *buf, bic);

            imb.srcAccessMask = vk::AccessFlagBits::eTransferRead;
            imb.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            imb.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
            imb.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            imb.srcQueueFamilyIndex = imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imb.image = *img;
            imb.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            cmd->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eFragmentShader, {}, 0, nullptr, 0, nullptr, 1, &imb);
        }
        cmd->end();

        vk::SubmitInfo si;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd.get();
        vk::UniqueFence submit_fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
        m_main_queue.submit(si, *submit_fence);
        m_dev->waitForFences(*submit_fence, true, UINT64_MAX);

        float* ptr = reinterpret_cast<float*>(m_dev->mapMemory(*buf_mem, 0, pix_sz));
        stbi_write_hdr(path.string().c_str(), sz.x, sz.y, 4, ptr);
        m_dev->unmapMemory(*buf_mem);
    }

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

    virtual void on_init() override
    {
        rt.create(m_pd, m_dev, 2048, 2048);
        m_tex.create(m_pd, m_dev, m_main_queue, m_cmd_pool, "brush.png");
        m_sampler = create_sampler(m_dev);
        render_finished_sem = m_dev->createSemaphoreUnique(vk::SemaphoreCreateInfo());
        clear_rt();

        m_cmd_strokes.resize(100);
        m_cmd_strokes_cmd.resize(m_cmd_strokes.size());
        for (int i = 0; i < m_cmd_strokes.size(); i++)
        {
            m_cmd_strokes[i].m_cleared = true;
            m_cmd_strokes[i].create(m_dev, m_pd, m_cmd_pool, m_descr_pool, rt.m_descr_layout, rt.m_renderpass,
                rt.m_framebuffer, rt.m_pipeline, rt.m_layout, m_sampler, vk::Extent2D(rt.m_size.x, rt.m_size.y),
                rt.m_fb_img, rt.m_fb_view, m_tex.m_view, { 0, 1, 0 });
            
            m_cmd_strokes_cmd[i] = *m_cmd_strokes[i].m_cmd;
        }
    }

    virtual void on_render_frame(float dt) override
    {
        static float theta = 0;
        static int cycle = 0;

        //if (cycle < 3)
        {
            cycle++;
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
            si.commandBufferCount = m_cmd_strokes_cmd.size();
            si.pCommandBuffers = m_cmd_strokes_cmd.data();
            vk::UniqueFence strokes_fence = m_dev->createFenceUnique(vk::FenceCreateInfo());
            m_main_queue.submit(si, *strokes_fence);
            m_dev->waitForFences(*strokes_fence, true, UINT64_MAX);
        }

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
            m_cmd_screen[i].m_ubo.m_value.mvp = glm::identity<glm::mat4>();
            m_cmd_screen[i].m_ubo.update(m_dev);
        }
    }

};

int main()
{
    auto app = std::make_unique<DrawApp>();
    app->on_keyup(0);
    app->init_vulkan();
    app->run_loop();
}
