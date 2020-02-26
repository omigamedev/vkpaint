#pragma once

using cs = vk::ComponentSwizzle;
using cc = vk::ColorComponentFlagBits;

struct vertex_t
{
    glm::vec3 pos;
    glm::vec3 col;
    glm::vec2 tex;
    vertex_t() = default;
    constexpr vertex_t(glm::vec3 p, glm::vec3 c, glm::vec2 t) : pos(p), col(c), tex(t) {}
};

int find_memory(const vk::PhysicalDevice& pd, const vk::MemoryRequirements& req, vk::MemoryPropertyFlags flags);
std::vector<uint8_t> read_file(const std::filesystem::path& path);
vk::UniqueShaderModule load_shader(const vk::UniqueDevice& dev, const std::filesystem::path& path);

extern bool swapchain_needs_recreation;
extern glm::ivec2 cur_pos;
extern glm::ivec2 wnd_size;
HWND create_window(int width, int height);

std::tuple<vk::UniqueImage, vk::UniqueImageView, vk::UniqueDeviceMemory> 
    create_depth(const vk::PhysicalDevice& pd, vk::Device const& dev, int width, int height);
std::tuple<vk::UniqueImage, vk::UniqueDeviceMemory, vk::UniqueImageView>
    create_texture(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev,
    const vk::Queue& q, const vk::UniqueCommandPool& cmd_pool);
vk::UniqueSampler create_sampler(const vk::UniqueDevice& dev);
auto create_triangle(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev);
std::tuple<vk::UniqueSwapchainKHR, vk::Extent2D>
    create_swapchain(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev, const vk::UniqueSurfaceKHR& surf);

template<typename T>
class UBO
{
public:
    vk::UniqueBuffer m_buffer;
    vk::UniqueDeviceMemory m_memory;
    T m_value;
    bool create(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev)
    {
        auto info = vk::BufferCreateInfo({}, sizeof(T), vk::BufferUsageFlagBits::eUniformBuffer);
        m_buffer = dev->createBufferUnique(info);
        auto mem_req = dev->getBufferMemoryRequirements(*m_buffer);
        auto mem_idx = find_memory(pd, mem_req,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        m_memory = dev->allocateMemoryUnique({ mem_req.size, (uint32_t)mem_idx });
        dev->bindBufferMemory(*m_buffer, *m_memory, 0);
        return true;
    }
    void update(const vk::UniqueDevice& dev)
    {
        if (auto uniform_map = static_cast<T*>(dev->mapMemory(*m_memory, 0, VK_WHOLE_SIZE)))
        {
            std::copy_n(&m_value, 1, uniform_map);
            dev->unmapMemory(*m_memory);
        }
    }
    static UBO<T> create_static(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev)
    {
        UBO<T> ubo;
        if (!ubo.create(pd, dev))
            throw std::runtime_error("UBO creation failed");
        return ubo;
    }
};

