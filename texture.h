#pragma once
#include "utils.h"

class Texture
{
public:
    vk::UniqueImage m_img;
    vk::UniqueImageView m_view;
    vk::UniqueDeviceMemory m_mem;

    bool create(const vk::PhysicalDevice& pd, const vk::UniqueDevice& dev, const vk::Queue& q, 
        const vk::UniqueCommandPool& cmd_pool, const std::filesystem::path& path);
};
