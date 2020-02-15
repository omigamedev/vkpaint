#pragma once

#include <iostream>
#include <sstream>
#include <array>
#include <vector>
#include <fstream>
#include <filesystem>
#include <windows.h>
#include <windowsx.h>
#include <fmt/format.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.hpp>

#define GLM_FORCE_RADIANS
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/euler_angles.hpp"
#include "glm/gtx/transform.hpp"

#include "stb/stb_image.h"
#include "tiny_obj_loader.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
