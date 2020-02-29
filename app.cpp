#include "pch.h"
#include "utils.h"
#include "app.h"
#include "debug_message.h"

bool App::init_vulkan()
{
    // create vulkan instance
    vk::ApplicationInfo app_info("VulkanTest", VK_MAKE_VERSION(0, 0, 1), "VulkanEngine", 1, VK_API_VERSION_1_1);
    std::vector<const char*> inst_layers{
#ifdef _DEBUG
        "VK_LAYER_LUNARG_standard_validation",
        "VK_LAYER_KHRONOS_validation",
        //"VK_LAYER_LUNARG_api_dump",
        "VK_LAYER_RENDERDOC_Capture",
#endif
    };
    std::vector<const char*> inst_ext{
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#ifdef _DEBUG
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
    };
    vk::InstanceCreateInfo create_info({}, &app_info,
        inst_layers.size(), inst_layers.data(), inst_ext.size(), inst_ext.data());
    m_instance = vk::createInstanceUnique(create_info);

    for (auto dl : vk::enumerateInstanceLayerProperties())
    {
        std::cout << "instance layer " << dl.layerName << ": " << dl.description << "\n";
    }

#ifdef _DEBUG
    init_debug_message(m_instance);
#endif

    m_wnd = create_window(800, 600);

    auto surf_info = vk::Win32SurfaceCreateInfoKHR({}, GetModuleHandle(0), m_wnd);
    m_surf = m_instance->createWin32SurfaceKHRUnique(surf_info);

    std::tie(m_pd, m_dev, m_family_idx) = find_device();

    // set window title to device name
    auto props = m_pd.getProperties();
    m_device_name = props.deviceName;
    std::string title = fmt::format("Vulkan {}", m_device_name);
    SetWindowTextA(m_wnd, title.c_str());

    m_main_queue = m_dev->getQueue(m_family_idx, 0);
    auto cmd_pool_info = vk::CommandPoolCreateInfo({}, m_family_idx);
    m_cmd_pool = m_dev->createCommandPoolUnique(cmd_pool_info);

    std::array<vk::DescriptorPoolSize, 2> descr_pool_size = {
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1000),
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1000),
    };
    auto descr_pool_info = vk::DescriptorPoolCreateInfo(
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        2000, descr_pool_size.size(), descr_pool_size.data());
    m_descr_pool = m_dev->createDescriptorPoolUnique(descr_pool_info);

    init_pipeline();
    on_init();

    resize();

    return true;
}


bool App::init_pipeline()
{
    m_vert_module = load_shader(m_dev, "shader-fill.vert.spv");
    m_frag_module = load_shader(m_dev, "shader-fill.frag.spv");

    vk::PipelineShaderStageCreateInfo pipeline_stages[] = {
        { {}, vk::ShaderStageFlagBits::eVertex, *m_vert_module, "main", nullptr },
        { {}, vk::ShaderStageFlagBits::eFragment, *m_frag_module, "main", nullptr }
    };
    auto pipeline_vertex_input = vk::PipelineVertexInputStateCreateInfo();
    auto pipeline_input_assembly = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleList, false);
    auto pipeline_vp = vk::Viewport(0, 0, m_swapchain_extent.width, m_swapchain_extent.height, 0, 1);
    auto pipeline_vpscissor = vk::Rect2D({ 0, 0 }, m_swapchain_extent);
    auto pipeline_vpstate = vk::PipelineViewportStateCreateInfo({}, 1, &pipeline_vp, 1, &pipeline_vpscissor);
    auto pipeline_raster = vk::PipelineRasterizationStateCreateInfo({}, false, false, vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise, false, 0, 0, 0, 1.f);
    auto pipeline_ms = vk::PipelineMultisampleStateCreateInfo({}, vk::SampleCountFlagBits::e1, false, 1.f, nullptr, false, false);
    vk::PipelineColorBlendAttachmentState pipeline_blend_state;
    pipeline_blend_state.blendEnable = false;
    pipeline_blend_state.colorWriteMask = cc::eR | cc::eG | cc::eB | cc::eA;
    auto pipeline_blend = vk::PipelineColorBlendStateCreateInfo({}, false, vk::LogicOp::eCopy, 1, &pipeline_blend_state);
    std::array<vk::DynamicState, 2> pipeline_dyn_states{
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };
    auto pipeline_dyn = vk::PipelineDynamicStateCreateInfo({},
        pipeline_dyn_states.size(), pipeline_dyn_states.data());

    std::array<vk::DescriptorSetLayoutBinding, 2> pipeline_layout_bind = {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer,
            1, vk::ShaderStageFlagBits::eVertex, nullptr),
        vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler,
            1, vk::ShaderStageFlagBits::eFragment, nullptr),
    };
    auto pipeline_layout_descr_info = vk::DescriptorSetLayoutCreateInfo({},
        pipeline_layout_bind.size(), pipeline_layout_bind.data());
    m_descr_layout = m_dev->createDescriptorSetLayoutUnique(pipeline_layout_descr_info);
    auto pipeline_layout_info = vk::PipelineLayoutCreateInfo({}, 1, &m_descr_layout.get(), 0, nullptr);
    m_pipeline_layout = m_dev->createPipelineLayoutUnique(pipeline_layout_info);

    auto pipeline_renderpass_fb = vk::AttachmentDescription({}, vk::Format::eB8G8R8A8Unorm,
        vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR);
    auto pipeline_subpass_color_ref = vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal);
    auto pipeline_subpass = vk::SubpassDescription({}, vk::PipelineBindPoint::eGraphics, 0, nullptr, 1, &pipeline_subpass_color_ref);
    auto pipeline_renderpass_info = vk::RenderPassCreateInfo({}, 1, &pipeline_renderpass_fb, 1, &pipeline_subpass, 0, nullptr);
    m_renderpass = m_dev->createRenderPassUnique(pipeline_renderpass_info);

    auto pipeline_info = vk::GraphicsPipelineCreateInfo({},
        2, pipeline_stages,
        &pipeline_vertex_input,
        &pipeline_input_assembly,
        nullptr,
        &pipeline_vpstate,
        &pipeline_raster,
        &pipeline_ms,
        nullptr,
        &pipeline_blend,
        &pipeline_dyn,
        *m_pipeline_layout,
        *m_renderpass, 0,
        nullptr, 0);
    m_pipeline = m_dev->createGraphicsPipelineUnique(nullptr, pipeline_info);
    return true;
}

std::tuple<vk::PhysicalDevice, vk::UniqueDevice, uint32_t> App::find_device()
{
    auto devices = m_instance->enumeratePhysicalDevices();
    for (auto& pd : devices)
    {
        auto qf_props = pd.getQueueFamilyProperties();
        for (int idx = 0; idx < qf_props.size(); idx++)
        {
            if (qf_props[idx].queueFlags & vk::QueueFlagBits::eGraphics && pd.getSurfaceSupportKHR(idx, *m_surf))
            {
                float priority = 0.0f;
                auto queue_info = vk::DeviceQueueCreateInfo({}, idx, 1u, &priority);
                std::vector<const char*> inst_layers;
                std::vector<const char*> inst_ext{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
                vk::PhysicalDeviceFeatures dev_feat;
                dev_feat.samplerAnisotropy = true;
                auto dev_info = vk::DeviceCreateInfo({}, 1, &queue_info,
                    inst_layers.size(), inst_layers.data(), inst_ext.size(), inst_ext.data(), &dev_feat);
                if (auto dev = pd.createDeviceUnique(dev_info))
                {
                    return { pd, std::move(dev), idx };
                }
            }
        }
    }
    return {};
}

void App::resize()
{
    create_swapchain();
    on_resize();
}

void App::create_swapchain()
{
    auto surface_formats = m_pd.getSurfaceFormatsKHR(*m_surf);
    auto surface_caps = m_pd.getSurfaceCapabilitiesKHR(*m_surf);
    auto swap_info = vk::SwapchainCreateInfoKHR({}, *m_surf, surface_caps.minImageCount,
        vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear, surface_caps.currentExtent, 1,
        vk::ImageUsageFlagBits::eColorAttachment,
        vk::SharingMode::eExclusive, 0, nullptr,
        vk::SurfaceTransformFlagBitsKHR::eIdentity, vk::CompositeAlphaFlagBitsKHR::eOpaque,
        vk::PresentModeKHR::eFifo, true, nullptr);
    m_swapchain = m_dev->createSwapchainKHRUnique(swap_info);
    m_swapchain_extent = surface_caps.currentExtent;

    m_swapchain_images = m_dev->getSwapchainImagesKHR(*m_swapchain);
    m_swapchain_views.resize(m_swapchain_images.size());
    m_framebuffers.resize(m_swapchain_images.size());

//     m_dev->resetDescriptorPool(*m_descr_pool);
//     m_dev->resetCommandPool(*m_cmd_pool, vk::CommandPoolResetFlags());

    for (size_t image_index = 0; image_index < m_swapchain_images.size(); image_index++)
    {
        auto view_info = vk::ImageViewCreateInfo({}, m_swapchain_images[image_index],
            vk::ImageViewType::e2D, vk::Format::eB8G8R8A8Unorm,
            vk::ComponentMapping(cs::eR, cs::eG, cs::eB, cs::eA),
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
        m_swapchain_views[image_index] = m_dev->createImageViewUnique(view_info);

        auto fb_info = vk::FramebufferCreateInfo({}, *m_renderpass, 1, &m_swapchain_views[image_index].get(),
            m_swapchain_extent.width, m_swapchain_extent.height, 1);
        m_framebuffers[image_index] = m_dev->createFramebufferUnique(fb_info);
    }
}

void App::run_loop()
{
    MSG msg;
    auto timer_start = std::chrono::high_resolution_clock::now();
    uint32_t frames = 0;
    float timer_fps = 0;
    while (true)
    {
        if (PeekMessage(&msg, m_wnd, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (swapchain_needs_recreation)
        {
            //resize();
            swapchain_needs_recreation = false;
        }

        auto timer_stop = std::chrono::high_resolution_clock::now();
        auto timer_diff = std::chrono::duration<float>(timer_stop - timer_start);
        timer_start = timer_stop;
        float dt = timer_diff.count();
        on_render_frame(dt);
        
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
    m_dev->waitIdle();
}
