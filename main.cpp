#include <iostream>
#include <ply.h>

#include "core/include/TDevice.h"
#include "core/include/TDeviceQueue.h"
#include "core/include/TEngine.h"
#include "core/include/TPhysicalDevice.h"
#include "core/include/TVulkanAllocator.h"

#include "core/include/TBuffer.h"
#include "core/include/TCommandBuffer.h"
#include "core/include/TCommandBufferPool.h"
#include "core/include/TImage.h"
#include "core/include/TImageView.h"

#include "core/include/TShader.h"

#include "core/include/TAttachment.h"
#include "core/include/TComputePipeline.h"
#include "core/include/TGraphicsPipeline.h"
#include "core/include/TRenderPass.h"
#include "core/include/TSubpass.h"

#include "core/include/TDescriptorPool.h"
#include "core/include/TDescriptorSet.h"
#include "core/include/TDescriptorSetLayout.h"
#include "core/include/TFramebuffer.h"

#include "core/include/TFence.h"
#include "core/include/TSemaphore.h"

#include <fstream>

#include <GLFW/glfw3.h>

#include "core/include/TSurface.h"
#include "core/include/TSwapchain.h"

#include <math.h>

#include "core/include/TPipelineDescriptorSet.h"
#include "core/include/TSampler.h"

#include <glm/ext.hpp>

#include "core/include/TVulkanLoader.h"
#include "glm/fwd.hpp"
#include "glm/geometric.hpp"
#include "glm/trigonometric.hpp"

#include <imgui.h>

static bool g_MouseJustPressed[ImGuiMouseButton_COUNT] = { false };
static GLFWcursor* g_MouseCursors[ImGuiMouseCursor_COUNT] = { nullptr };

std::string ReadTextFile(const std::string& filename)
{
    std::ifstream fileStream(filename);
    if (!fileStream.is_open())
    {
        throw std::runtime_error("Unable to open file: " + filename);
    }
    return { std::istreambuf_iterator<char>(fileStream), std::istreambuf_iterator<char>() };
}

#define TEX_SIZE 512

typedef struct POSITION
{
    float x, y, z, w;
} POSITION;

typedef struct COLOR
{
    float r, g, b, a;
} COLOR;

struct MATRIXS_BUFFER_DATA
{
    glm::mat4 m, v, p;
};

typedef struct Point
{
    POSITION position;
    COLOR color;
} Point;

typedef struct PlyData
{
    std::vector<Point> points;
    POSITION min, max;
} PlyData;

const std::string IMGUI_VERT_SHADER_STR = ReadTextFile("./shaders/imgui.vert");
const std::string IMGUI_FRAG_SHADER_STR = ReadTextFile("./shaders/imgui.frag");
const std::string MY_VERT_SHADER_STR = ReadTextFile("./shaders/PointCloud.vert");
const std::string MY_FRAG_SHADER_STR = ReadTextFile("./shaders/PointCloud.frag");

PlyData LoadPly(const std::string& url)
{
    PlyData plyData;
    plyData.min = { FLT_MAX, FLT_MAX, FLT_MAX, 0 };
    plyData.max = { -FLT_MAX, -FLT_MAX, -FLT_MAX, 0 };

    int elementsCount = 0;
    char** elements;
    int fileType;
    float version;

    PlyFile* plyFile = ply_open_for_reading(const_cast<char*>(url.c_str()), &elementsCount, &elements, &fileType, &version);
    if (plyFile)
    {
        for (int elemIdx = 0; elemIdx < elementsCount; ++elemIdx)
        {
            if (equal_strings("vertex", elements[elemIdx]))
            {
                int numElems = 0;
                int numProps = 0;

                PlyProperty** propList = ply_get_element_description(plyFile, elements[elemIdx], &numElems, &numProps);  
                struct PlyVertex
                {
                    float x, y, z;
                    unsigned char r, g, b, a;
                };

                PlyProperty vertProps[] = {
                    {"x", PLY_FLOAT, PLY_FLOAT, offsetof(PlyVertex, x), 0, 0, 0, 0},
                    {"y", PLY_FLOAT, PLY_FLOAT, offsetof(PlyVertex, y), 0, 0, 0, 0},
                    {"z", PLY_FLOAT, PLY_FLOAT, offsetof(PlyVertex, z), 0, 0, 0, 0},
                    {"red", PLY_UCHAR, PLY_UCHAR, offsetof(PlyVertex, r), 0, 0, 0, 0},
                    {"green", PLY_UCHAR, PLY_UCHAR, offsetof(PlyVertex, g), 0, 0, 0, 0},
                    {"blue", PLY_UCHAR, PLY_UCHAR, offsetof(PlyVertex, b), 0, 0, 0, 0},
                    {"alpha", PLY_UCHAR, PLY_UCHAR, offsetof(PlyVertex, a), 0, 0, 0, 0},
                };

                for (PlyProperty& prop : vertProps)
                {
                    ply_get_property(plyFile, elements[elemIdx], &prop);
                }

                for (int i = 0; i < numElems; ++i)
                {
                    PlyVertex vertex;
                    ply_get_element(plyFile, &vertex);

                    Point point;
                    point.position = { vertex.x, vertex.y, vertex.z, 0.0f };
                    point.color = { vertex.r / 255.0f, vertex.g / 255.0f, vertex.b / 255.0f, vertex.a / 255.0f };

                    plyData.points.push_back(point);

                    plyData.min.x = std::min(plyData.min.x, point.position.x);
                    plyData.min.y = std::min(plyData.min.y, point.position.y);
                    plyData.min.z = std::min(plyData.min.z, point.position.z);

                    plyData.max.x = std::max(plyData.max.x, point.position.x);
                    plyData.max.y = std::max(plyData.max.y, point.position.y);
                    plyData.max.z = std::max(plyData.max.z, point.position.z);
                }
            }
        }
        ply_close(plyFile);
    }
    else
    {
        std::cerr << "Failed to open ply file." << std::endl;
    }

    return plyData;
}


typedef struct PointsPositionImage
{
    Turbo::Core::TRefPtr<Turbo::Core::TImage> image;
    Turbo::Core::TRefPtr<Turbo::Core::TImageView> imageView;
} PointsPositionImage;

typedef struct PointsColorImage
{
    Turbo::Core::TRefPtr<Turbo::Core::TImage> image;
    Turbo::Core::TRefPtr<Turbo::Core::TImageView> imageView;
} PointsColorImage;

typedef struct PointsImageData
{
    PointsPositionImage pointsPositionImage;
    PointsColorImage pointsColorImage;
    uint32_t count = 0;
} PointsImageData;

std::vector<Point> PlyDatasToPoints(const std::vector<PlyData>& plyDatas)
{
    std::vector<Point> points;
    for (const PlyData& plyData : plyDatas)
    {
        points.insert(points.end(), plyData.points.begin(), plyData.points.end());
    }
    return points;
}
std::vector<PointsImageData> CreateAllPointsImageData(const std::vector<Point>& points, Turbo::Core::TRefPtr<Turbo::Core::TDevice> device, Turbo::Core::TRefPtr<Turbo::Core::TDeviceQueue> queue, Turbo::Core::TRefPtr<Turbo::Core::TCommandBufferPool> commandPool)
{
    std::vector<PointsImageData> result;
    size_t tex_size = TEX_SIZE;
    size_t tex_content_size = tex_size * tex_size;
    size_t loop_count = points.size() / tex_content_size;
    size_t residue_count = points.size() % tex_content_size;

    auto create_points_image_data = [&](const std::vector<POSITION>& pointsPosition, const std::vector<COLOR>& pointsColor) -> PointsImageData {
        PointsImageData imageData;
        Turbo::Core::TRefPtr<Turbo::Core::TBuffer> positionBuffer = new Turbo::Core::TBuffer(device, 0, Turbo::Core::TBufferUsageBits::BUFFER_TRANSFER_SRC, Turbo::Core::TMemoryFlagsBits::HOST_ACCESS_SEQUENTIAL_WRITE, pointsPosition.size() * sizeof(POSITION));
        Turbo::Core::TRefPtr<Turbo::Core::TBuffer> colorBuffer = new Turbo::Core::TBuffer(device, 0, Turbo::Core::TBufferUsageBits::BUFFER_TRANSFER_SRC, Turbo::Core::TMemoryFlagsBits::HOST_ACCESS_SEQUENTIAL_WRITE, pointsColor.size() * sizeof(COLOR));

        void* positionPtr = positionBuffer->Map();
        memcpy(positionPtr, pointsPosition.data(), pointsPosition.size() * sizeof(POSITION));
        positionBuffer->Unmap();

        void* colorPtr = colorBuffer->Map();
        memcpy(colorPtr, pointsColor.data(), pointsColor.size() * sizeof(COLOR));
        colorBuffer->Unmap();

        Turbo::Core::TRefPtr<Turbo::Core::TImage> positionImage = new Turbo::Core::TImage(device, 0, Turbo::Core::TImageType::DIMENSION_2D, Turbo::Core::TFormatType::R32G32B32A32_SFLOAT, tex_size, tex_size, 1, 1, 1, Turbo::Core::TSampleCountBits::SAMPLE_1_BIT, Turbo::Core::TImageTiling::OPTIMAL, Turbo::Core::TImageUsageBits::IMAGE_TRANSFER_DST | Turbo::Core::TImageUsageBits::IMAGE_SAMPLED | Turbo::Core::TImageUsageBits::IMAGE_STORAGE, Turbo::Core::TMemoryFlagsBits::DEDICATED_MEMORY, Turbo::Core::TImageLayout::UNDEFINED);
        Turbo::Core::TRefPtr<Turbo::Core::TImage> colorImage = new Turbo::Core::TImage(device, 0, Turbo::Core::TImageType::DIMENSION_2D, Turbo::Core::TFormatType::R32G32B32A32_SFLOAT, tex_size, tex_size, 1, 1, 1, Turbo::Core::TSampleCountBits::SAMPLE_1_BIT, Turbo::Core::TImageTiling::OPTIMAL, Turbo::Core::TImageUsageBits::IMAGE_TRANSFER_DST | Turbo::Core::TImageUsageBits::IMAGE_SAMPLED | Turbo::Core::TImageUsageBits::IMAGE_STORAGE, Turbo::Core::TMemoryFlagsBits::DEDICATED_MEMORY, Turbo::Core::TImageLayout::UNDEFINED);

        Turbo::Core::TRefPtr<Turbo::Core::TCommandBuffer> commandBuffer = commandPool->Allocate();
        commandBuffer->Begin();

        commandBuffer->CmdTransformImageLayout(Turbo::Core::TPipelineStageBits::HOST_BIT, Turbo::Core::TPipelineStageBits::TRANSFER_BIT, Turbo::Core::TAccessBits::HOST_WRITE_BIT, Turbo::Core::TAccessBits::TRANSFER_WRITE_BIT, Turbo::Core::TImageLayout::UNDEFINED, Turbo::Core::TImageLayout::TRANSFER_DST_OPTIMAL, positionImage, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 1, 0, 1);
        commandBuffer->CmdTransformImageLayout(Turbo::Core::TPipelineStageBits::HOST_BIT, Turbo::Core::TPipelineStageBits::TRANSFER_BIT, Turbo::Core::TAccessBits::HOST_WRITE_BIT, Turbo::Core::TAccessBits::TRANSFER_WRITE_BIT, Turbo::Core::TImageLayout::UNDEFINED, Turbo::Core::TImageLayout::TRANSFER_DST_OPTIMAL, colorImage, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 1, 0, 1);

        size_t rowCount = pointsPosition.size() / tex_size;
        size_t remainingPoints = pointsPosition.size() % tex_size;

        commandBuffer->CmdCopyBufferToImage(positionBuffer, positionImage, Turbo::Core::TImageLayout::TRANSFER_DST_OPTIMAL, 0, 0, 0, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 0, 1, 0, 0, 0, tex_size, rowCount, 1);
        commandBuffer->CmdCopyBufferToImage(colorBuffer, colorImage, Turbo::Core::TImageLayout::TRANSFER_DST_OPTIMAL, 0, 0, 0, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 0, 1, 0, 0, 0, tex_size, rowCount, 1);

        if (remainingPoints > 0)
        {
            commandBuffer->CmdCopyBufferToImage(positionBuffer, positionImage, Turbo::Core::TImageLayout::TRANSFER_DST_OPTIMAL, rowCount * tex_size * sizeof(POSITION), 0, 0, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 0, 1, 0, rowCount, 0, remainingPoints, 1, 1);
            commandBuffer->CmdCopyBufferToImage(colorBuffer, colorImage, Turbo::Core::TImageLayout::TRANSFER_DST_OPTIMAL, rowCount * tex_size * sizeof(COLOR), 0, 0, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 0, 1, 0, rowCount, 0, remainingPoints, 1, 1);
        }

        commandBuffer->CmdTransformImageLayout(Turbo::Core::TPipelineStageBits::TRANSFER_BIT, Turbo::Core::TPipelineStageBits::FRAGMENT_SHADER_BIT, Turbo::Core::TAccessBits::TRANSFER_WRITE_BIT, Turbo::Core::TAccessBits::SHADER_READ_BIT, Turbo::Core::TImageLayout::TRANSFER_DST_OPTIMAL, Turbo::Core::TImageLayout::GENERAL, positionImage, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 1, 0, 1);
        commandBuffer->CmdTransformImageLayout(Turbo::Core::TPipelineStageBits::TRANSFER_BIT, Turbo::Core::TPipelineStageBits::FRAGMENT_SHADER_BIT, Turbo::Core::TAccessBits::TRANSFER_WRITE_BIT, Turbo::Core::TAccessBits::SHADER_READ_BIT, Turbo::Core::TImageLayout::TRANSFER_DST_OPTIMAL, Turbo::Core::TImageLayout::GENERAL, colorImage, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 1, 0, 1);

        commandBuffer->End();

        Turbo::Core::TRefPtr<Turbo::Core::TFence> fence = new Turbo::Core::TFence(device);
        queue->Submit(commandBuffer, fence);
        fence->WaitUntil();
        commandPool->Free(commandBuffer);

        Turbo::Core::TRefPtr<Turbo::Core::TImageView> positionImageView = new Turbo::Core::TImageView(positionImage, Turbo::Core::TImageViewType::IMAGE_VIEW_2D, positionImage->GetFormat(), Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 1, 0, 1);
        Turbo::Core::TRefPtr<Turbo::Core::TImageView> colorImageView = new Turbo::Core::TImageView(colorImage, Turbo::Core::TImageViewType::IMAGE_VIEW_2D, colorImage->GetFormat(), Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 1, 0, 1);

        imageData.pointsPositionImage.image = positionImage;
        imageData.pointsPositionImage.imageView = positionImageView;
        imageData.pointsColorImage.image = colorImage;
        imageData.pointsColorImage.imageView = colorImageView;
        imageData.count = pointsPosition.size();

        return imageData;
        };

    for (size_t loopIndex = 0; loopIndex < loop_count; ++loopIndex)
    {
        size_t offset = loopIndex * tex_content_size;

        std::vector<POSITION> pointsPosition;
        std::vector<COLOR> pointsColor;

        for (size_t i = 0; i < tex_content_size; ++i)
        {
            pointsPosition.push_back(points[offset + i].position);
            pointsColor.push_back(points[offset + i].color);
        }

        result.push_back(create_points_image_data(pointsPosition, pointsColor));
    }

    if (residue_count > 0)
    {
        size_t offset = loop_count * tex_content_size;

        std::vector<POSITION> pointsPosition;
        std::vector<COLOR> pointsColor;

        for (size_t i = 0; i < residue_count; ++i)
        {
            pointsPosition.push_back(points[offset + i].position);
            pointsColor.push_back(points[offset + i].color);
        }

        result.push_back(create_points_image_data(pointsPosition, pointsColor));
    }

    return result;
}


int main()
{
    std::vector<PlyData> ply_datas;
    {
        //ply_datas.push_back(LoadPly("./models/points.ply"));
        //ply_datas.push_back(LoadPly("./models/sy-carola-point-cloud/source/Carola_PointCloud/Carola_PointCloud.ply"));
        ply_datas.push_back(LoadPly("./models/bigbuilding/source/seu_vella_jardi_claustre_7M/seu_vella_jardi_claustre_7M.ply"));

        //ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0000_00_vh_clean_2.labels.ply"));
        //ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0001_00/scene0001_00_vh_clean_2.labels.ply"));
        /*
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0002_00/scene0002_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0003_00/scene0003_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0004_00/scene0004_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0005_00/scene0005_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0006_00/scene0006_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0007_00/scene0007_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0008_00/scene0008_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0009_00/scene0009_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0010_00/scene0010_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0011_00/scene0011_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0012_00/scene0012_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0013_00/scene0013_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0014_00/scene0014_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0015_00/scene0015_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0016_00/scene0016_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0017_00/scene0017_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0018_00/scene0018_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0019_00/scene0019_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0020_00/scene0020_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0021_00/scene0021_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0022_00/scene0022_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0023_00/scene0023_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0024_00/scene0024_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0025_00/scene0025_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0026_00/scene0026_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0027_00/scene0027_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0028_00/scene0028_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0029_00/scene0029_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0030_00/scene0030_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0031_00/scene0031_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0032_00/scene0032_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0033_00/scene0033_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0034_00/scene0034_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0035_00/scene0035_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0036_00/scene0036_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0037_00/scene0037_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0038_00/scene0038_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0039_00/scene0039_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0040_00/scene0040_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0041_00/scene0041_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0042_00/scene0042_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0043_00/scene0043_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0044_00/scene0044_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0045_00/scene0045_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0046_00/scene0046_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0047_00/scene0047_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0048_00/scene0048_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0049_00/scene0049_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0050_00/scene0050_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0051_00/scene0051_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0052_00/scene0052_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0053_00/scene0053_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0054_00/scene0054_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0055_00/scene0055_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0056_00/scene0056_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0057_00/scene0057_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0058_00/scene0058_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0059_00/scene0059_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0060_00/scene0060_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0061_00/scene0061_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0062_00/scene0062_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0063_00/scene0063_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0064_00/scene0064_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0065_00/scene0065_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0066_00/scene0066_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0067_00/scene0067_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0068_00/scene0068_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0069_00/scene0069_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0070_00/scene0070_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0071_00/scene0071_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0072_00/scene0072_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0073_00/scene0073_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0074_00/scene0074_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0075_00/scene0075_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0076_00/scene0076_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0077_00/scene0077_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0078_00/scene0078_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0079_00/scene0079_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0080_00/scene0080_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0081_00/scene0081_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0082_00/scene0082_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0083_00/scene0083_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0084_00/scene0084_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0085_00/scene0085_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0086_00/scene0086_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0087_00/scene0087_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0088_00/scene0088_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0089_00/scene0089_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0090_00/scene0090_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0091_00/scene0091_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0092_00/scene0092_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0093_00/scene0093_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0094_00/scene0094_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0095_00/scene0095_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0096_00/scene0096_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0097_00/scene0097_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0098_00/scene0098_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0099_00/scene0099_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0100_00/scene0100_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0101_00/scene0101_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0102_00/scene0102_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0103_00/scene0103_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0104_00/scene0104_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0105_00/scene0105_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0106_00/scene0106_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0107_00/scene0107_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0108_00/scene0108_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0109_00/scene0109_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0110_00/scene0110_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0111_00/scene0111_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0112_00/scene0112_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0113_00/scene0113_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0114_00/scene0114_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0115_00/scene0115_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0116_00/scene0116_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0117_00/scene0117_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0118_00/scene0118_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0119_00/scene0119_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0120_00/scene0120_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0121_00/scene0121_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0122_00/scene0122_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0123_00/scene0123_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0124_00/scene0124_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0125_00/scene0125_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0126_00/scene0126_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0127_00/scene0127_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0128_00/scene0128_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0129_00/scene0129_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0130_00/scene0130_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0131_00/scene0131_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0132_00/scene0132_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0133_00/scene0133_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0134_00/scene0134_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0135_00/scene0135_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0136_00/scene0136_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0137_00/scene0137_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0138_00/scene0138_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0139_00/scene0139_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0140_00/scene0140_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0141_00/scene0141_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0142_00/scene0142_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0143_00/scene0143_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0144_00/scene0144_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0145_00/scene0145_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0146_00/scene0146_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0147_00/scene0147_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0148_00/scene0148_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0149_00/scene0149_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0150_00/scene0150_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0151_00/scene0151_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0152_00/scene0152_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0153_00/scene0153_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0154_00/scene0154_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0155_00/scene0155_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0156_00/scene0156_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0157_00/scene0157_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0158_00/scene0158_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0159_00/scene0159_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0160_00/scene0160_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0161_00/scene0161_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0162_00/scene0162_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0163_00/scene0163_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0164_00/scene0164_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0165_00/scene0165_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0166_00/scene0166_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0167_00/scene0167_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0168_00/scene0168_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0169_00/scene0169_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0170_00/scene0170_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0171_00/scene0171_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0172_00/scene0172_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0173_00/scene0173_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0174_00/scene0174_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0175_00/scene0175_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0176_00/scene0176_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0177_00/scene0177_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0178_00/scene0178_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0179_00/scene0179_00_vh_clean_2.labels.ply"));
        ply_datas.push_back(LoadPly("E:/study/pointcloud/PointCloud/models/scannet/scans/scene0180_00/scene0180_00_vh_clean_2.labels.ply"));
        */
        //... 在此增加其他 ply 点云数据;
   }

   auto points = PlyDatasToPoints(ply_datas);
   ply_datas.clear();

   size_t all_point_count = points.size();

   std::cout << "points::size::" << points.size() << ":: ----------------------------------------------------------------------------------" << std::endl;
   std::cout << "Vulkan Version:" << Turbo::Core::TVulkanLoader::Instance()->GetVulkanVersion().ToString() << ":: ----------------------------------------------------------------------------------" << std::endl;

   std::vector<Turbo::Core::TLayerInfo> support_layers;
   std::vector<Turbo::Core::TExtensionInfo> instance_support_extensions;
   {
       Turbo::Core::TRefPtr<Turbo::Core::TInstance> temp_instance = new Turbo::Core::TInstance();
       support_layers = temp_instance->GetSupportLayers();
       instance_support_extensions = temp_instance->GetSupportExtensions();
   }

   Turbo::Core::TLayerInfo khronos_validation;
   for (const auto& layer : support_layers)
   {
       if (layer.GetLayerType() == Turbo::Core::TLayerType::VK_LAYER_KHRONOS_VALIDATION)
       {
           khronos_validation = layer;
           break;
       }
   }

   std::vector<Turbo::Core::TLayerInfo> enable_layer;
   if (khronos_validation.GetLayerType() != Turbo::Core::TLayerType::UNDEFINED)
   {
       enable_layer.push_back(khronos_validation);
   }

   std::vector<Turbo::Core::TExtensionInfo> enable_instance_extensions;
   for (const auto& extension : instance_support_extensions)
   {
       switch (extension.GetExtensionType())
       {
       case Turbo::Core::TExtensionType::VK_KHR_SURFACE:
       case Turbo::Core::TExtensionType::VK_KHR_WIN32_SURFACE:
       case Turbo::Core::TExtensionType::VK_KHR_WAYLAND_SURFACE:
       case Turbo::Core::TExtensionType::VK_KHR_XCB_SURFACE:
       case Turbo::Core::TExtensionType::VK_KHR_XLIB_SURFACE:
           enable_instance_extensions.push_back(extension);
           break;
       default:
           break;
       }
   }

   Turbo::Core::TVersion instance_version(1, 2, 0, 0);
   Turbo::Core::TRefPtr<Turbo::Core::TInstance> instance = new Turbo::Core::TInstance(&enable_layer, &enable_instance_extensions, &instance_version);
   Turbo::Core::TRefPtr<Turbo::Core::TPhysicalDevice> physical_device = instance->GetBestPhysicalDevice();

   if (!glfwInit())
       return -1;

   GLFWwindow* window;
   int window_width = 1920 / 2;
   int window_height = 1080 / 2;
   glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
   window = glfwCreateWindow(window_width, window_height, "Turbo", nullptr, nullptr);

   VkSurfaceKHR vk_surface_khr = VK_NULL_HANDLE;
   VkInstance vk_instance = instance->GetVkInstance();
   glfwCreateWindowSurface(vk_instance, window, nullptr, &vk_surface_khr);

   Turbo::Core::TPhysicalDeviceFeatures physical_device_features = {};
   physical_device_features.sampleRateShading = true;
   physical_device_features.fillModeNonSolid = true;

   std::vector<Turbo::Core::TExtensionInfo> enable_device_extensions;
   auto physical_device_support_extensions = physical_device->GetSupportExtensions();
   for (const auto& extension : physical_device_support_extensions)
   {
       if (extension.GetExtensionType() == Turbo::Core::TExtensionType::VK_KHR_SWAPCHAIN)
       {
           enable_device_extensions.push_back(extension);
       }
   }

   Turbo::Core::TRefPtr<Turbo::Core::TDevice> device = new Turbo::Core::TDevice(physical_device, nullptr, &enable_device_extensions, &physical_device_features);
   Turbo::Core::TRefPtr<Turbo::Core::TDeviceQueue> queue = device->GetBestGraphicsQueue();

   Turbo::Core::TRefPtr<Turbo::Extension::TSurface> surface = new Turbo::Extension::TSurface(device, nullptr, vk_surface_khr);
   uint32_t max_image_count = surface->GetMaxImageCount();
   uint32_t min_image_count = surface->GetMinImageCount();
   //uint32_t swapchain_image_count = (max_image_count <= min_image_count) ? min_image_count : max_image_count - 1;
   uint32_t swapchain_image_count = max_image_count;


   Turbo::Core::TRefPtr<Turbo::Extension::TSwapchain> swapchain = new Turbo::Extension::TSwapchain(surface, swapchain_image_count, Turbo::Core::TFormatType::B8G8R8A8_SRGB, 1, Turbo::Core::TImageUsageBits::IMAGE_COLOR_ATTACHMENT | Turbo::Core::TImageUsageBits::IMAGE_TRANSFER_SRC | Turbo::Core::TImageUsageBits::IMAGE_TRANSFER_DST, true);

   std::vector<Turbo::Core::TRefPtr<Turbo::Core::TImage>> swapchain_images = swapchain->GetImages();
   std::vector<Turbo::Core::TRefPtr<Turbo::Core::TImageView>> swapchain_image_views;

   for (const auto& swapchain_image_item : swapchain_images)
   {
       Turbo::Core::TRefPtr<Turbo::Core::TImageView> swapchain_view = new Turbo::Core::TImageView(swapchain_image_item, Turbo::Core::TImageViewType::IMAGE_VIEW_2D, Turbo::Core::TFormatType::B8G8R8A8_SRGB, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 1, 0, 1);
       swapchain_image_views.push_back(swapchain_view);
   }

   Turbo::Core::TRefPtr<Turbo::Core::TCommandBufferPool> command_pool = new Turbo::Core::TCommandBufferPool(queue);
   Turbo::Core::TRefPtr<Turbo::Core::TCommandBuffer> command_buffer = command_pool->Allocate();

   auto all_points_image_data = CreateAllPointsImageData(points, device, queue, command_pool);
   points.clear();

   MATRIXS_BUFFER_DATA matrixs_buffer_data = {};

   glm::mat4 model = glm::mat4(1.0f);
   glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-10.0f, 0.0f, 0.0f));
   glm::mat4 projection = glm::perspective(glm::radians(45.0f), static_cast<float>(swapchain->GetWidth()) / swapchain->GetHeight(), 0.1f, 100.0f);

   matrixs_buffer_data.m = model;
   matrixs_buffer_data.v = view;
   matrixs_buffer_data.p = projection;

   Turbo::Core::TRefPtr<Turbo::Core::TBuffer> matrixs_buffer = new Turbo::Core::TBuffer(device, 0, Turbo::Core::TBufferUsageBits::BUFFER_UNIFORM_BUFFER | Turbo::Core::TBufferUsageBits::BUFFER_TRANSFER_DST, Turbo::Core::TMemoryFlagsBits::HOST_ACCESS_SEQUENTIAL_WRITE, sizeof(matrixs_buffer_data));
   void* mvp_ptr = matrixs_buffer->Map();
   memcpy(mvp_ptr, &matrixs_buffer_data, sizeof(matrixs_buffer_data));
   matrixs_buffer->Unmap();

   Turbo::Core::TRefPtr<Turbo::Core::TImage> depth_image = new Turbo::Core::TImage(device, 0, Turbo::Core::TImageType::DIMENSION_2D, Turbo::Core::TFormatType::D32_SFLOAT, swapchain->GetWidth(), swapchain->GetHeight(), 1, 1, 1, Turbo::Core::TSampleCountBits::SAMPLE_1_BIT, Turbo::Core::TImageTiling::OPTIMAL, Turbo::Core::TImageUsageBits::IMAGE_DEPTH_STENCIL_ATTACHMENT | Turbo::Core::TImageUsageBits::IMAGE_INPUT_ATTACHMENT, Turbo::Core::TMemoryFlagsBits::DEDICATED_MEMORY, Turbo::Core::TImageLayout::UNDEFINED);
   Turbo::Core::TRefPtr<Turbo::Core::TImageView> depth_image_view = new Turbo::Core::TImageView(depth_image, Turbo::Core::TImageViewType::IMAGE_VIEW_2D, depth_image->GetFormat(), Turbo::Core::TImageAspectBits::ASPECT_DEPTH_BIT, 0, 1, 0, 1);

   Turbo::Core::TRefPtr<Turbo::Core::TVertexShader> my_vertex_shader = new Turbo::Core::TVertexShader(device, Turbo::Core::TShaderLanguage::GLSL, MY_VERT_SHADER_STR);
   Turbo::Core::TRefPtr<Turbo::Core::TFragmentShader> my_fragment_shader = new Turbo::Core::TFragmentShader(device, Turbo::Core::TShaderLanguage::GLSL, MY_FRAG_SHADER_STR);

   std::vector<Turbo::Core::TDescriptorSize> descriptor_sizes = {
       {Turbo::Core::TDescriptorType::UNIFORM_BUFFER, 1000},
       {Turbo::Core::TDescriptorType::COMBINED_IMAGE_SAMPLER, 1000},
       {Turbo::Core::TDescriptorType::SAMPLER, 1000},
       {Turbo::Core::TDescriptorType::SAMPLED_IMAGE, 1000},
       {Turbo::Core::TDescriptorType::STORAGE_IMAGE, 1000},
       {Turbo::Core::TDescriptorType::UNIFORM_TEXEL_BUFFER, 1000},
       {Turbo::Core::TDescriptorType::STORAGE_TEXEL_BUFFER, 1000},
       {Turbo::Core::TDescriptorType::STORAGE_BUFFER, 1000},
       {Turbo::Core::TDescriptorType::UNIFORM_BUFFER_DYNAMIC, 1000},
       {Turbo::Core::TDescriptorType::STORAGE_BUFFER_DYNAMIC, 1000},
       {Turbo::Core::TDescriptorType::INPUT_ATTACHMENT, 1000} };

   Turbo::Core::TRefPtr<Turbo::Core::TDescriptorPool> descriptor_pool = new Turbo::Core::TDescriptorPool(device, descriptor_sizes.size() * 1000, descriptor_sizes);

   Turbo::Core::TSubpass subpass(Turbo::Core::TPipelineType::Graphics);
   subpass.AddColorAttachmentReference(0, Turbo::Core::TImageLayout::COLOR_ATTACHMENT_OPTIMAL);                // swapchain color image
   subpass.SetDepthStencilAttachmentReference(1, Turbo::Core::TImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL); // depth image

   Turbo::Core::TSubpass subpass1(Turbo::Core::TPipelineType::Graphics);
   subpass1.AddColorAttachmentReference(0, Turbo::Core::TImageLayout::COLOR_ATTACHMENT_OPTIMAL); // swapchain color image

   std::vector<Turbo::Core::TSubpass> subpasses = { subpass, subpass1 };

   Turbo::Core::TAttachment swapchain_color_attachment(swapchain_images[0]->GetFormat(), swapchain_images[0]->GetSampleCountBits(), Turbo::Core::TLoadOp::CLEAR, Turbo::Core::TStoreOp::STORE, Turbo::Core::TLoadOp::DONT_CARE, Turbo::Core::TStoreOp::DONT_CARE, Turbo::Core::TImageLayout::UNDEFINED, Turbo::Core::TImageLayout::PRESENT_SRC_KHR);
   Turbo::Core::TAttachment depth_attachment(depth_image->GetFormat(), depth_image->GetSampleCountBits(), Turbo::Core::TLoadOp::CLEAR, Turbo::Core::TStoreOp::STORE, Turbo::Core::TLoadOp::DONT_CARE, Turbo::Core::TStoreOp::DONT_CARE, Turbo::Core::TImageLayout::UNDEFINED, Turbo::Core::TImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

   std::vector<Turbo::Core::TAttachment> attachments = { swapchain_color_attachment, depth_attachment };

   Turbo::Core::TRefPtr<Turbo::Core::TRenderPass> render_pass = new Turbo::Core::TRenderPass(device, attachments, subpasses);
   Turbo::Core::TViewport viewport(0, 0, surface->GetCurrentWidth(), surface->GetCurrentHeight(), 0, 1);
   Turbo::Core::TScissor scissor(0, 0, surface->GetCurrentWidth(), surface->GetCurrentHeight());

   std::vector<Turbo::Core::TVertexBinding> vertex_bindings;

   Turbo::Core::TRefPtr<Turbo::Core::TGraphicsPipeline> graphics_pipeline = new Turbo::Core::TGraphicsPipeline(render_pass, 0, vertex_bindings, my_vertex_shader, my_fragment_shader, Turbo::Core::TTopologyType::POINT_LIST, false, false, false, Turbo::Core::TPolygonMode::POINT, Turbo::Core::TCullModeBits::MODE_BACK_BIT, Turbo::Core::TFrontFace::CLOCKWISE, false, 0, 0, 0, 1, false, Turbo::Core::TSampleCountBits::SAMPLE_1_BIT, true, true, Turbo::Core::TCompareOp::LESS_OR_EQUAL, false, false, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TCompareOp::ALWAYS, 0, 0, 0, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TCompareOp::ALWAYS, 0, 0, 0, 0, 0, false, Turbo::Core::TLogicOp::NO_OP, true, Turbo::Core::TBlendFactor::SRC_ALPHA, Turbo::Core::TBlendFactor::ONE_MINUS_SRC_ALPHA, Turbo::Core::TBlendOp::ADD, Turbo::Core::TBlendFactor::ONE_MINUS_SRC_ALPHA, Turbo::Core::TBlendFactor::ZERO, Turbo::Core::TBlendOp::ADD);
    // Turbo::Core::TRefPtr<Turbo::Core::TGraphicsPipeline> graphics_pipeline = new Turbo::Core::TGraphicsPipeline(render_pass, 0, vertex_bindings, my_vertex_shader, my_fragment_shader, Turbo::Core::TTopologyType::POINT_LIST, false, false, false, Turbo::Core::TPolygonMode::POINT, Turbo::Core::TCullModeBits::MODE_BACK_BIT, Turbo::Core::TFrontFace::CLOCKWISE, false, 0, 0, 0, 1, false, Turbo::Core::TSampleCountBits::SAMPLE_1_BIT, false, false, Turbo::Core::TCompareOp::LESS_OR_EQUAL, false, false, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TCompareOp::ALWAYS, 0, 0, 0, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TCompareOp::ALWAYS, 0, 0, 0, 0, 0, false, Turbo::Core::TLogicOp::NO_OP, true, Turbo::Core::TBlendFactor::SRC_ALPHA, Turbo::Core::TBlendFactor::ONE_MINUS_SRC_ALPHA, Turbo::Core::TBlendOp::ADD, Turbo::Core::TBlendFactor::ONE_MINUS_SRC_ALPHA, Turbo::Core::TBlendFactor::ZERO, Turbo::Core::TBlendOp::ADD);
   std::vector<Turbo::Core::TRefPtr<Turbo::Core::TPipelineDescriptorSet>> graphics_pipeline_descriptor_sets;
   for (const auto& points_image_data_item : all_points_image_data)
   {
       Turbo::Core::TRefPtr<Turbo::Core::TPipelineDescriptorSet> pipeline_descriptor_set = descriptor_pool->Allocate(graphics_pipeline->GetPipelineLayout());
       std::vector<Turbo::Core::TRefPtr<Turbo::Core::TImageView>> points_pos_image_views = { points_image_data_item.pointsPositionImage.imageView };
       std::vector<Turbo::Core::TRefPtr<Turbo::Core::TImageView>> points_color_image_views = { points_image_data_item.pointsColorImage.imageView };
       std::vector<Turbo::Core::TRefPtr<Turbo::Core::TBuffer>> matrixs_buffers = { matrixs_buffer };

       pipeline_descriptor_set->BindData(0, 0, 0, matrixs_buffers);
       pipeline_descriptor_set->BindData(0, 1, 0, points_pos_image_views);
       pipeline_descriptor_set->BindData(0, 2, 0, points_color_image_views);

       graphics_pipeline_descriptor_sets.push_back(pipeline_descriptor_set);
   }

    std::vector<Turbo::Core::TRefPtr<Turbo::Core::TFramebuffer>> swpachain_framebuffers;
    for (Turbo::Core::TRefPtr<Turbo::Core::TImageView> swapchain_image_view_item : swapchain_image_views)
    {
        std::vector<Turbo::Core::TRefPtr<Turbo::Core::TImageView>> image_views;
        image_views.push_back(swapchain_image_view_item);
        image_views.push_back(depth_image_view);

        Turbo::Core::TRefPtr<Turbo::Core::TFramebuffer> swapchain_framebuffer = new Turbo::Core::TFramebuffer(render_pass, image_views);
        swpachain_framebuffers.push_back(swapchain_framebuffer);
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    auto imgui_sampler = Turbo::Core::TRefPtr<Turbo::Core::TSampler>(new Turbo::Core::TSampler(device));
    auto imgui_vertex_shader = Turbo::Core::TRefPtr<Turbo::Core::TShader>(new Turbo::Core::TShader(device, Turbo::Core::TShaderType::VERTEX, Turbo::Core::TShaderLanguage::GLSL, IMGUI_VERT_SHADER_STR));
    auto imgui_fragment_shader = Turbo::Core::TRefPtr<Turbo::Core::TShader>(new Turbo::Core::TShader(device, Turbo::Core::TShaderType::FRAGMENT, Turbo::Core::TShaderLanguage::GLSL, IMGUI_FRAG_SHADER_STR));

    Turbo::Core::TVertexBinding imgui_vertex_binding(0, sizeof(ImDrawVert), Turbo::Core::TVertexRate::VERTEX);
    imgui_vertex_binding.AddAttribute(0, Turbo::Core::TFormatType::R32G32_SFLOAT, IM_OFFSETOF(ImDrawVert, pos));
    imgui_vertex_binding.AddAttribute(1, Turbo::Core::TFormatType::R32G32_SFLOAT, IM_OFFSETOF(ImDrawVert, uv));
    imgui_vertex_binding.AddAttribute(2, Turbo::Core::TFormatType::R8G8B8A8_UNORM, IM_OFFSETOF(ImDrawVert, col));

    std::vector<Turbo::Core::TRefPtr<Turbo::Core::TShader>> imgui_shaders = { imgui_vertex_shader, imgui_fragment_shader };
    std::vector<Turbo::Core::TVertexBinding> imgui_vertex_bindings = { imgui_vertex_binding };

    auto imgui_pipeline = Turbo::Core::TRefPtr<Turbo::Core::TGraphicsPipeline>(new Turbo::Core::TGraphicsPipeline(render_pass, 1, imgui_vertex_bindings, imgui_shaders, Turbo::Core::TTopologyType::TRIANGLE_LIST, false, false, false, Turbo::Core::TPolygonMode::FILL, Turbo::Core::TCullModeBits::MODE_BACK_BIT, Turbo::Core::TFrontFace::CLOCKWISE, false, 0, 0, 0, 1, false, Turbo::Core::TSampleCountBits::SAMPLE_1_BIT, false, false, Turbo::Core::TCompareOp::LESS_OR_EQUAL, false, false, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TCompareOp::ALWAYS, 0, 0, 0, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TStencilOp::KEEP, Turbo::Core::TCompareOp::ALWAYS, 0, 0, 0, 0, 0, false, Turbo::Core::TLogicOp::NO_OP, true, Turbo::Core::TBlendFactor::SRC_ALPHA, Turbo::Core::TBlendFactor::ONE_MINUS_SRC_ALPHA, Turbo::Core::TBlendOp::ADD, Turbo::Core::TBlendFactor::ONE_MINUS_SRC_ALPHA, Turbo::Core::TBlendFactor::ZERO, Turbo::Core::TBlendOp::ADD));

    unsigned char* imgui_font_pixels;
    int imgui_font_width, imgui_font_height;
    io.Fonts->GetTexDataAsRGBA32(&imgui_font_pixels, &imgui_font_width, &imgui_font_height);
    size_t imgui_upload_size = imgui_font_width * imgui_font_height * 4 * sizeof(char);

    auto imgui_font_image = Turbo::Core::TRefPtr<Turbo::Core::TImage>(new Turbo::Core::TImage(device, 0, Turbo::Core::TImageType::DIMENSION_2D, Turbo::Core::TFormatType::R8G8B8A8_UNORM, imgui_font_width, imgui_font_height, 1, 1, 1, Turbo::Core::TSampleCountBits::SAMPLE_1_BIT, Turbo::Core::TImageTiling::OPTIMAL, Turbo::Core::TImageUsageBits::IMAGE_SAMPLED | Turbo::Core::TImageUsageBits::IMAGE_TRANSFER_DST, Turbo::Core::TMemoryFlagsBits::DEDICATED_MEMORY));
    auto imgui_font_image_view = Turbo::Core::TRefPtr<Turbo::Core::TImageView>(new Turbo::Core::TImageView(imgui_font_image, Turbo::Core::TImageViewType::IMAGE_VIEW_2D, imgui_font_image->GetFormat(), Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 1, 0, 1));

    auto imgui_font_buffer = Turbo::Core::TRefPtr<Turbo::Core::TBuffer>(new Turbo::Core::TBuffer(device, 0, Turbo::Core::TBufferUsageBits::BUFFER_TRANSFER_SRC, Turbo::Core::TMemoryFlagsBits::HOST_ACCESS_SEQUENTIAL_WRITE, imgui_upload_size));
    void* imgui_font_ptr = imgui_font_buffer->Map();
    memcpy(imgui_font_ptr, imgui_font_pixels, imgui_upload_size);
    imgui_font_buffer->Unmap();

    auto imgui_copy_command_buffer = command_pool->Allocate();
    imgui_copy_command_buffer->Begin();
    imgui_copy_command_buffer->CmdTransformImageLayout(Turbo::Core::TPipelineStageBits::HOST_BIT, Turbo::Core::TPipelineStageBits::TRANSFER_BIT, Turbo::Core::TAccessBits::HOST_WRITE_BIT, Turbo::Core::TAccessBits::TRANSFER_WRITE_BIT, Turbo::Core::TImageLayout::UNDEFINED, Turbo::Core::TImageLayout::TRANSFER_DST_OPTIMAL, imgui_font_image, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 1, 0, 1);
    imgui_copy_command_buffer->CmdCopyBufferToImage(imgui_font_buffer, imgui_font_image, Turbo::Core::TImageLayout::TRANSFER_DST_OPTIMAL, 0, imgui_font_width, imgui_font_height, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 0, 1, 0, 0, 0, imgui_font_width, imgui_font_height, 1);
    imgui_copy_command_buffer->CmdTransformImageLayout(Turbo::Core::TPipelineStageBits::TRANSFER_BIT, Turbo::Core::TPipelineStageBits::FRAGMENT_SHADER_BIT, Turbo::Core::TAccessBits::TRANSFER_WRITE_BIT, Turbo::Core::TAccessBits::SHADER_READ_BIT, Turbo::Core::TImageLayout::TRANSFER_DST_OPTIMAL, Turbo::Core::TImageLayout::SHADER_READ_ONLY_OPTIMAL, imgui_font_image, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 1, 0, 1);
    imgui_copy_command_buffer->End();

    auto imgui_font_copy_fence = Turbo::Core::TRefPtr<Turbo::Core::TFence>(new Turbo::Core::TFence(device));
    queue->Submit(imgui_copy_command_buffer, imgui_font_copy_fence);
    imgui_font_copy_fence->WaitUntil();

    std::vector<std::pair<Turbo::Core::TRefPtr<Turbo::Core::TImageView>, Turbo::Core::TRefPtr<Turbo::Core::TSampler>>> imgui_combined_image_samplers = { std::make_pair(imgui_font_image_view, imgui_sampler) };

    Turbo::Core::TRefPtr<Turbo::Core::TPipelineDescriptorSet> imgui_pipeline_descriptor_set = descriptor_pool->Allocate(imgui_pipeline->GetPipelineLayout());
    imgui_pipeline_descriptor_set->BindData(0, 0, 0, imgui_combined_image_samplers);

    io.Fonts->TexID = (ImTextureID)(intptr_t)(imgui_font_image->GetVkImage());

    Turbo::Core::TRefPtr<Turbo::Core::TBuffer> imgui_vertex_buffer = nullptr;
    Turbo::Core::TRefPtr<Turbo::Core::TBuffer> imgui_index_buffer = nullptr;
    // </IMGUI>

    glm::vec3 camera_position(0.0f, 0.0f, 0.0f);
    glm::vec3 look_forward(0.0f, 0.0f, 1.0f);

    float horizontal_angle = 0.0f;
    float vertical_angle = 0.0f;

    glm::vec2 previous_mouse_pos(0.0f, 0.0f);
    glm::vec2 current_mouse_pos(0.0f, 0.0f);

    float angle = 0.0f;
    float _time = glfwGetTime();

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // <Begin Rendering>
        uint32_t current_image_index = UINT32_MAX;
        Turbo::Core::TRefPtr<Turbo::Core::TSemaphore> wait_image_ready = new Turbo::Core::TSemaphore(device, Turbo::Core::TPipelineStageBits::COLOR_ATTACHMENT_OUTPUT_BIT);
        Turbo::Core::TResult result = swapchain->AcquireNextImageUntil(wait_image_ready, nullptr, &current_image_index);

        if (result == Turbo::Core::TResult::SUCCESS)
        {
            int window_w, window_h;
            int display_w, display_h;
            glfwGetWindowSize(window, &window_w, &window_h);
            glfwGetFramebufferSize(window, &display_w, &display_h);
            io.DisplaySize = ImVec2((float)window_w, (float)window_h);
            if (window_w > 0 && window_h > 0)
            {
                io.DisplayFramebufferScale = ImVec2((float)display_w / window_w, (float)display_h / window_h);
            }

            double current_time = glfwGetTime();
            io.DeltaTime = _time > 0.0f ? (float)(current_time - _time) : (float)(1.0f / 60.0f);
            _time = current_time;

            // Update Mouse and Keyboard
            {
                ImGuiIO& io = ImGui::GetIO();
                for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); i++)
                {
                    io.MouseDown[i] = g_MouseJustPressed[i] || glfwGetMouseButton(window, i) != 0;
                    g_MouseJustPressed[i] = false;
                }

                const ImVec2 mouse_pos_backup = io.MousePos;
                io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);

                if (glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0)
                {
                    if (io.WantSetMousePos)
                    {
                        glfwSetCursorPos(window, (double)mouse_pos_backup.x, (double)mouse_pos_backup.y);
                    }
                    else
                    {
                        double mouse_x, mouse_y;
                        glfwGetCursorPos(window, &mouse_x, &mouse_y);
                        io.MousePos = ImVec2((float)mouse_x, (float)mouse_y);
                    }
                }

                if (!(io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) && glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
                {
                    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
                    if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
                    {
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                    }
                    else
                    {
                        glfwSetCursor(window, g_MouseCursors[imgui_cursor] ? g_MouseCursors[imgui_cursor] : g_MouseCursors[ImGuiMouseCursor_Arrow]);
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    }
                }

                ImVec2 mouse_pos = io.MousePos;
                current_mouse_pos = glm::vec2(mouse_pos.x, mouse_pos.y);
                glm::vec2 mouse_pos_delta = current_mouse_pos - previous_mouse_pos;
                previous_mouse_pos = current_mouse_pos;
                mouse_pos_delta.y = -mouse_pos_delta.y;

                if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
                {
                    horizontal_angle += mouse_pos_delta.x * 0.2f;
                    vertical_angle += mouse_pos_delta.y * 0.2f;

                    vertical_angle = glm::clamp(vertical_angle, -90.0f, 90.0f);
                }

                float delta_time = io.DeltaTime;
                float speed = 1.0f;

                glm::mat4 forward_rotate_mat = glm::rotate(glm::mat4(1.0f), glm::radians(horizontal_angle), glm::vec3(0.0f, 0.0f, 1.0f));
                forward_rotate_mat = glm::rotate(forward_rotate_mat, glm::radians(-vertical_angle), glm::vec3(0.0f, 1.0f, 0.0f));

                look_forward = glm::normalize(glm::vec3(forward_rotate_mat * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)));

                glm::vec3 forward_dir = look_forward;
                glm::vec3 up_dir = glm::vec3(0.0f, 0.0f, 1.0f);
                glm::vec3 right_dir = glm::normalize(glm::cross(up_dir, forward_dir));
                up_dir = glm::normalize(glm::cross(right_dir, forward_dir));

                if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
                {
                    camera_position += forward_dir * speed * delta_time;
                }
                if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
                {
                    camera_position += -right_dir * speed * delta_time;
                }
                if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
                {
                    camera_position += -forward_dir * speed * delta_time;
                }
                if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
                {
                    camera_position += right_dir * speed * delta_time;
                }

                model = glm::mat4(1.0f);
                glm::vec3 look_point = camera_position + forward_dir;
                view = glm::lookAt(camera_position, look_point, up_dir);
                projection = glm::perspective(glm::radians(45.0f), (float)(swapchain->GetWidth() <= 0 ? 1 : swapchain->GetWidth()) / (float)(swapchain->GetHeight() <= 0 ? 1 : swapchain->GetHeight()), 0.1f, 300.0f);

                matrixs_buffer_data.m = model;
                matrixs_buffer_data.v = view;
                matrixs_buffer_data.p = projection;

                void* _ptr = matrixs_buffer->Map();
                memcpy(_ptr, &matrixs_buffer_data, sizeof(matrixs_buffer_data));
                matrixs_buffer->Unmap();
            }

            ImGui::NewFrame();
            {
                ImGui::Begin("PointCloud");
                ImGui::Text("W,A,S,D to move.");
                ImGui::Text("Push down and drag mouse right button to rotate view.");
                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
                ImGui::Text((std::string("All point count : ") + std::to_string(all_point_count)).c_str());
                ImGui::End();
            }

            // Graphics Pipeline Draw
            Turbo::Core::TViewport frame_viewport(0, 0, swapchain->GetWidth() <= 0 ? 1 : swapchain->GetWidth(), swapchain->GetHeight(), 0, 1);
            Turbo::Core::TScissor frame_scissor(0, 0, swapchain->GetWidth() <= 0 ? 1 : swapchain->GetWidth(), swapchain->GetHeight() <= 0 ? 1 : swapchain->GetHeight());

            command_buffer->Begin();
            command_buffer->CmdBeginRenderPass(render_pass, swpachain_framebuffers[current_image_index]);
            command_buffer->CmdBindPipeline(graphics_pipeline);
            command_buffer->CmdSetViewport({ frame_viewport });
            command_buffer->CmdSetScissor({ frame_scissor });

            for (size_t points_image_index = 0; points_image_index < all_points_image_data.size(); points_image_index++)
            {
                command_buffer->CmdBindPipelineDescriptorSet(graphics_pipeline_descriptor_sets[points_image_index]);
                command_buffer->CmdDraw(1, all_points_image_data[points_image_index].count, 0, 0);
            }

            command_buffer->CmdNextSubpass();

            // <IMGUI Rendering>
            ImGui::Render();
            ImDrawData* draw_data = ImGui::GetDrawData();
            bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

            if (!is_minimized && draw_data->TotalVtxCount > 0)
            {
                int fb_width = static_cast<int>(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
                int fb_height = static_cast<int>(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);

                if (fb_width > 0 && fb_height > 0)
                {
                    size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
                    size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

                    imgui_vertex_buffer = nullptr;
                    imgui_index_buffer = nullptr;

                    imgui_vertex_buffer = new Turbo::Core::TBuffer(device, 0, Turbo::Core::TBufferUsageBits::BUFFER_VERTEX_BUFFER, Turbo::Core::TMemoryFlagsBits::HOST_ACCESS_SEQUENTIAL_WRITE, vertex_size);
                    imgui_index_buffer = new Turbo::Core::TBuffer(device, 0, Turbo::Core::TBufferUsageBits::BUFFER_INDEX_BUFFER, Turbo::Core::TMemoryFlagsBits::HOST_ACCESS_SEQUENTIAL_WRITE, index_size);

                    ImDrawVert* vtx_dst = static_cast<ImDrawVert*>(imgui_vertex_buffer->Map());
                    ImDrawIdx* idx_dst = static_cast<ImDrawIdx*>(imgui_index_buffer->Map());

                    for (int n = 0; n < draw_data->CmdListsCount; ++n)
                    {
                        const ImDrawList* cmd_list = draw_data->CmdLists[n];
                        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
                        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
                        vtx_dst += cmd_list->VtxBuffer.Size;
                        idx_dst += cmd_list->IdxBuffer.Size;
                    }

                    imgui_vertex_buffer->Unmap();
                    imgui_index_buffer->Unmap();

                    command_buffer->CmdBindPipeline(imgui_pipeline);
                    command_buffer->CmdBindPipelineDescriptorSet(imgui_pipeline_descriptor_set);

                    command_buffer->CmdBindVertexBuffers({ imgui_vertex_buffer });
                    command_buffer->CmdBindIndexBuffer(imgui_index_buffer, 0, sizeof(ImDrawIdx) == 2 ? Turbo::Core::TIndexType::UINT16 : Turbo::Core::TIndexType::UINT32);

                    float scale[2] = { 2.0f / draw_data->DisplaySize.x, 2.0f / draw_data->DisplaySize.y };
                    float translate[2] = { -1.0f - draw_data->DisplayPos.x * scale[0], -1.0f - draw_data->DisplayPos.y * scale[1] };

                    command_buffer->CmdPushConstants(0, sizeof(scale), scale);
                    command_buffer->CmdPushConstants(sizeof(scale), sizeof(translate), translate);

                    ImVec2 clip_off = draw_data->DisplayPos;
                    ImVec2 clip_scale = draw_data->FramebufferScale;

                    int global_vtx_offset = 0;
                    int global_idx_offset = 0;

                    for (int n = 0; n < draw_data->CmdListsCount; ++n)
                    {
                        const ImDrawList* cmd_list = draw_data->CmdLists[n];

                        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i)
                        {
                            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];

                            if (pcmd->UserCallback)
                            {
                                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                                {
                                    command_buffer->CmdBindPipeline(imgui_pipeline);
                                    command_buffer->CmdBindPipelineDescriptorSet(imgui_pipeline_descriptor_set);
                                    command_buffer->CmdBindVertexBuffers({ imgui_vertex_buffer });
                                    command_buffer->CmdBindIndexBuffer(imgui_index_buffer, 0, sizeof(ImDrawIdx) == 2 ? Turbo::Core::TIndexType::UINT16 : Turbo::Core::TIndexType::UINT32);
                                    command_buffer->CmdPushConstants(0, sizeof(scale), scale);
                                    command_buffer->CmdPushConstants(sizeof(scale), sizeof(translate), translate);
                                }
                                else
                                {
                                    pcmd->UserCallback(cmd_list, pcmd);
                                }
                            }
                            else
                            {
                                ImVec4 clip_rect = {
                                    (pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                                    (pcmd->ClipRect.y - clip_off.y) * clip_scale.y,
                                    (pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                                    (pcmd->ClipRect.w - clip_off.y) * clip_scale.y
                                };

                                if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
                                {
                                    clip_rect.x = glm::max(clip_rect.x, 0.0f);
                                    clip_rect.y = glm::max(clip_rect.y, 0.0f);

                                    VkRect2D scissor = {
                                        { static_cast<int32_t>(clip_rect.x), static_cast<int32_t>(clip_rect.y) },
                                        { static_cast<uint32_t>(clip_rect.z - clip_rect.x), static_cast<uint32_t>(clip_rect.w - clip_rect.y) }
                                    };

                                    Turbo::Core::TScissor imgui_scissor(scissor.offset.x, scissor.offset.y, scissor.extent.width, scissor.extent.height);
                                    command_buffer->CmdSetScissor({ imgui_scissor });

                                    command_buffer->CmdDrawIndexed(pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset, 0);
                                }
                            }
                        }

                        global_idx_offset += cmd_list->IdxBuffer.Size;
                        global_vtx_offset += cmd_list->VtxBuffer.Size;
                    }
                }
            }

            command_buffer->CmdEndRenderPass();
            command_buffer->End();

            Turbo::Core::TRefPtr<Turbo::Core::TFence> fence = new Turbo::Core::TFence(device);
            queue->Submit({ wait_image_ready }, {}, command_buffer, fence);
            fence->WaitUntil();
            command_buffer->Reset();

            if (queue->Present(swapchain, current_image_index) == Turbo::Core::TResult::MISMATCH)
            {
                device->WaitIdle();

                swapchain_images.clear();
                swapchain_image_views.clear();
                swpachain_framebuffers.clear();

                Turbo::Core::TRefPtr<Turbo::Extension::TSwapchain> old_swapchain = swapchain;
                swapchain = new Turbo::Extension::TSwapchain(old_swapchain);

                swapchain_images = swapchain->GetImages();
                for (auto& swapchain_image_item : swapchain_images)
                {
                    auto swapchain_view = new Turbo::Core::TImageView(swapchain_image_item, Turbo::Core::TImageViewType::IMAGE_VIEW_2D, Turbo::Core::TFormatType::B8G8R8A8_SRGB, Turbo::Core::TImageAspectBits::ASPECT_COLOR_BIT, 0, 1, 0, 1);
                    swapchain_image_views.emplace_back(swapchain_view);
                }

                depth_image = new Turbo::Core::TImage(device, 0, Turbo::Core::TImageType::DIMENSION_2D, Turbo::Core::TFormatType::D32_SFLOAT, swapchain->GetWidth(), swapchain->GetHeight(), 1, 1, 1, Turbo::Core::TSampleCountBits::SAMPLE_1_BIT, Turbo::Core::TImageTiling::OPTIMAL, Turbo::Core::TImageUsageBits::IMAGE_DEPTH_STENCIL_ATTACHMENT | Turbo::Core::TImageUsageBits::IMAGE_INPUT_ATTACHMENT, Turbo::Core::TMemoryFlagsBits::DEDICATED_MEMORY, Turbo::Core::TImageLayout::UNDEFINED);
                depth_image_view = new Turbo::Core::TImageView(depth_image, Turbo::Core::TImageViewType::IMAGE_VIEW_2D, depth_image->GetFormat(), Turbo::Core::TImageAspectBits::ASPECT_DEPTH_BIT, 0, 1, 0, 1);

                for (const auto& image_view_item : swapchain_image_views)
                {
                    std::vector<Turbo::Core::TRefPtr<Turbo::Core::TImageView>> views{ image_view_item, depth_image_view };
                    swpachain_framebuffers.emplace_back(new Turbo::Core::TFramebuffer(render_pass, views));
                }
            }
        }
    }


    for (auto pipeline_descriptor_set_item : graphics_pipeline_descriptor_sets)
    {
        descriptor_pool->Free(pipeline_descriptor_set_item);
    }

    command_pool->Free(command_buffer);

    glfwTerminate();

    return 0;
}