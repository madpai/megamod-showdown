/* Vulkan backend. One implementation, two modes (swapchain / offscreen).
 * Kept deliberately small: no descriptor sets, no textures yet, no render
 * graph. Push constants carry the camera and BSP lighting. */
#include "gfx.h"
#include "../platform/platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ANDROID__)
#  define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#include <vulkan/vulkan.h>

/* glslc -mfmt=c already emits the enclosing braces, so do NOT add another
 * pair here: doing so silently truncates the array to its first word. */
static const uint32_t kMeshVert[] =
#include "../../shaders/mesh_vert.inl"
;
static const uint32_t kMeshFrag[] =
#include "../../shaders/mesh_frag.inl"
;

/* If the include braces are ever double-wrapped again, these fire at compile
 * time instead of failing on a device. */
_Static_assert(sizeof(kMeshVert) > 256, "mesh vertex SPIR-V looks truncated");
_Static_assert(sizeof(kMeshFrag) > 256, "mesh fragment SPIR-V looks truncated");
_Static_assert(sizeof(kMeshVert) % 4 == 0, "SPIR-V must be a whole number of words");

#define MAX_IMAGES 8
#define PUSH_SIZE  112u   /* mat4(64) + 3 * vec4(48) */

struct hta_gfx_mesh {
    VkBuffer       vbuf, ibuf;
    VkDeviceMemory vmem, imem;
    uint32_t       index_count;
    uint64_t       bytes;
};

struct hta_gfx {
    bool offscreen;

    VkInstance       instance;
    VkPhysicalDevice phys;
    VkDevice         device;
    uint32_t         qfamily;
    VkQueue          queue;
    VkPhysicalDeviceMemoryProperties memprops;
    char             device_name[256];
    uint64_t         mem_used;

    /* swapchain path */
    VkSurfaceKHR   surface;
    VkSwapchainKHR swapchain;

    /* offscreen path */
    VkImage        off_image;
    VkDeviceMemory off_mem;
    VkBuffer       readback_buf;
    VkDeviceMemory readback_mem;

    VkFormat    format;
    VkExtent2D  extent;
    uint32_t    image_count;
    VkImage     images[MAX_IMAGES];
    VkImageView views[MAX_IMAGES];
    VkFramebuffer fbs[MAX_IMAGES];

    VkFormat       depth_format;
    VkImage        depth_image;
    VkDeviceMemory depth_mem;
    VkImageView    depth_view;

    VkRenderPass     pass;
    VkPipelineLayout layout;
    VkPipeline       pipeline;

    VkCommandPool   pool;
    VkCommandBuffer cmd[MAX_IMAGES];
    VkSemaphore     sem_acquire[MAX_IMAGES];
    VkSemaphore     sem_release[MAX_IMAGES];
    VkFence         fence[MAX_IMAGES];
    uint32_t        frame;

    void *window;
    bool  ready;
};

static void gfail(char *err, size_t n, const char *fmt, ...)
{
    if (!err || !n) return;
    va_list ap; va_start(ap, fmt); vsnprintf(err, n, fmt, ap); va_end(ap);
}

#define VKREQ(expr, what) do { \
    VkResult _r = (expr); \
    if (_r != VK_SUCCESS) { gfail(err, errlen, "%s failed (VkResult %d)", what, (int)_r); return false; } \
} while (0)

static bool find_mem(const hta_gfx *g, uint32_t type_bits, VkMemoryPropertyFlags want,
                     uint32_t *out)
{
    for (uint32_t i = 0; i < g->memprops.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (g->memprops.memoryTypes[i].propertyFlags & want) == want) { *out = i; return true; }
    }
    return false;
}

static bool make_buffer(hta_gfx *g, VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags props, VkBuffer *buf, VkDeviceMemory *mem,
                        char *err, size_t errlen)
{
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VKREQ(vkCreateBuffer(g->device, &bi, NULL, buf), "vkCreateBuffer");

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(g->device, *buf, &mr);
    uint32_t type = 0;
    if (!find_mem(g, mr.memoryTypeBits, props, &type)) {
        gfail(err, errlen, "no memory type for buffer (bits 0x%X)", mr.memoryTypeBits);
        return false;
    }
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = type;
    VKREQ(vkAllocateMemory(g->device, &ai, NULL, mem), "vkAllocateMemory(buffer)");
    VKREQ(vkBindBufferMemory(g->device, *buf, *mem, 0), "vkBindBufferMemory");
    g->mem_used += mr.size;
    return true;
}

static bool make_image(hta_gfx *g, uint32_t w, uint32_t h, VkFormat fmt,
                       VkImageUsageFlags usage, VkImageAspectFlags aspect,
                       VkImage *img, VkDeviceMemory *mem, VkImageView *view,
                       char *err, size_t errlen)
{
    VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ii.imageType     = VK_IMAGE_TYPE_2D;
    ii.format        = fmt;
    ii.extent.width  = w;
    ii.extent.height = h;
    ii.extent.depth  = 1;
    ii.mipLevels     = 1;
    ii.arrayLayers   = 1;
    ii.samples       = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ii.usage         = usage;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKREQ(vkCreateImage(g->device, &ii, NULL, img), "vkCreateImage");

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(g->device, *img, &mr);
    uint32_t type = 0;
    if (!find_mem(g, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &type)) {
        gfail(err, errlen, "no device-local memory type for image");
        return false;
    }
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = mr.size;
    ai.memoryTypeIndex = type;
    VKREQ(vkAllocateMemory(g->device, &ai, NULL, mem), "vkAllocateMemory(image)");
    VKREQ(vkBindImageMemory(g->device, *img, *mem, 0), "vkBindImageMemory");
    g->mem_used += mr.size;

    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image    = *img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format   = fmt;
    vi.subresourceRange.aspectMask = aspect;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    VKREQ(vkCreateImageView(g->device, &vi, NULL, view), "vkCreateImageView");
    return true;
}

/* ------------------------------ setup ------------------------------ */

static bool create_instance(hta_gfx *g, bool want_surface, char *err, size_t errlen)
{
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "halo-trial-android";
    app.apiVersion       = VK_API_VERSION_1_1;

    const char *exts[4];
    uint32_t n = 0;
    if (want_surface) {
        exts[n++] = VK_KHR_SURFACE_EXTENSION_NAME;
#if defined(__ANDROID__)
        exts[n++] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
#endif
    }
    VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = n;
    ci.ppEnabledExtensionNames = n ? exts : NULL;
    VKREQ(vkCreateInstance(&ci, NULL, &g->instance), "vkCreateInstance");
    return true;
}

static bool pick_device(hta_gfx *g, char *err, size_t errlen)
{
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(g->instance, &n, NULL);
    if (n == 0) { gfail(err, errlen, "no Vulkan physical devices"); return false; }
    if (n > 8) n = 8;
    VkPhysicalDevice devs[8];
    vkEnumeratePhysicalDevices(g->instance, &n, devs);

    for (uint32_t i = 0; i < n; i++) {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, NULL);
        if (qn > 16) qn = 16;
        VkQueueFamilyProperties qp[16];
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, qp);
        for (uint32_t q = 0; q < qn; q++) {
            if (!(qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            if (g->surface) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], q, g->surface, &present);
                if (!present) continue;
            }
            g->phys = devs[i];
            g->qfamily = q;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devs[i], &props);
            snprintf(g->device_name, sizeof(g->device_name), "%s", props.deviceName);
            vkGetPhysicalDeviceMemoryProperties(devs[i], &g->memprops);
            return true;
        }
    }
    gfail(err, errlen, "no suitable graphics queue found");
    return false;
}

static bool create_device(hta_gfx *g, char *err, size_t errlen)
{
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = g->qfamily;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    const char *dexts[1];
    uint32_t dn = 0;
    if (!g->offscreen) dexts[dn++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    VkDeviceCreateInfo ci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = dn;
    ci.ppEnabledExtensionNames = dn ? dexts : NULL;
    VKREQ(vkCreateDevice(g->phys, &ci, NULL, &g->device), "vkCreateDevice");
    vkGetDeviceQueue(g->device, g->qfamily, 0, &g->queue);
    return true;
}

static VkFormat pick_depth(hta_gfx *g)
{
    const VkFormat cands[] = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT,
                               VK_FORMAT_D16_UNORM };
    for (unsigned i = 0; i < sizeof(cands)/sizeof(cands[0]); i++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(g->phys, cands[i], &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return cands[i];
    }
    return VK_FORMAT_D16_UNORM;
}

static bool create_targets(hta_gfx *g, uint32_t w, uint32_t h, char *err, size_t errlen)
{
    if (g->offscreen) {
        g->format      = VK_FORMAT_R8G8B8A8_UNORM;
        g->extent.width = w;
        g->extent.height = h;
        g->image_count = 1;
        if (!make_image(g, w, h, g->format,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        &g->off_image, &g->off_mem, &g->views[0], err, errlen)) return false;
        g->images[0] = g->off_image;

        if (!make_buffer(g, (VkDeviceSize)w * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &g->readback_buf, &g->readback_mem, err, errlen)) return false;
    } else {
        VkSurfaceCapabilitiesKHR caps;
        VKREQ(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g->phys, g->surface, &caps),
              "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        uint32_t fn = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(g->phys, g->surface, &fn, NULL);
        if (fn == 0) { gfail(err, errlen, "surface reports no formats"); return false; }
        if (fn > 32) fn = 32;
        VkSurfaceFormatKHR fmts[32];
        vkGetPhysicalDeviceSurfaceFormatsKHR(g->phys, g->surface, &fn, fmts);
        VkSurfaceFormatKHR chosen = fmts[0];
        for (uint32_t i = 0; i < fn; i++)
            if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
                fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) { chosen = fmts[i]; break; }

        g->format = chosen.format;
        g->extent = caps.currentExtent;
        if (g->extent.width == 0xFFFFFFFFu) { g->extent.width = w; g->extent.height = h; }
        if (g->extent.width == 0 || g->extent.height == 0) {
            gfail(err, errlen, "surface extent is zero (window not ready)");
            return false;
        }

        uint32_t want = caps.minImageCount + 1;
        if (caps.maxImageCount && want > caps.maxImageCount) want = caps.maxImageCount;
        if (want > MAX_IMAGES) want = MAX_IMAGES;

        VkSwapchainCreateInfoKHR sci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        sci.surface          = g->surface;
        sci.minImageCount    = want;
        sci.imageFormat      = chosen.format;
        sci.imageColorSpace  = chosen.colorSpace;
        sci.imageExtent      = g->extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform     = caps.currentTransform;
        sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped          = VK_TRUE;
        VKREQ(vkCreateSwapchainKHR(g->device, &sci, NULL, &g->swapchain), "vkCreateSwapchainKHR");

        g->image_count = MAX_IMAGES;
        VKREQ(vkGetSwapchainImagesKHR(g->device, g->swapchain, &g->image_count, g->images),
              "vkGetSwapchainImagesKHR");
        for (uint32_t i = 0; i < g->image_count; i++) {
            VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vi.image    = g->images[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format   = g->format;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            VKREQ(vkCreateImageView(g->device, &vi, NULL, &g->views[i]), "vkCreateImageView");
        }
    }

    /* shared depth buffer */
    g->depth_format = pick_depth(g);
    if (!make_image(g, g->extent.width, g->extent.height, g->depth_format,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
                    &g->depth_image, &g->depth_mem, &g->depth_view, err, errlen)) return false;
    return true;
}

static bool create_pass_pipeline(hta_gfx *g, char *err, size_t errlen)
{
    VkAttachmentDescription at[2];
    memset(at, 0, sizeof(at));
    at[0].format         = g->format;
    at[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    at[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    at[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    at[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    at[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    at[0].finalLayout    = g->offscreen ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                        : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    at[1].format         = g->depth_format;
    at[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    at[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    at[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    at[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    at[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference cref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference dref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub;
    memset(&sub, 0, sizeof(sub));
    sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount    = 1;
    sub.pColorAttachments       = &cref;
    sub.pDepthStencilAttachment = &dref;

    VkSubpassDependency dep;
    memset(&dep, 0, sizeof(dep));
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rp.attachmentCount = 2; rp.pAttachments  = at;
    rp.subpassCount    = 1; rp.pSubpasses    = &sub;
    rp.dependencyCount = 1; rp.pDependencies = &dep;
    VKREQ(vkCreateRenderPass(g->device, &rp, NULL, &g->pass), "vkCreateRenderPass");

    for (uint32_t i = 0; i < g->image_count; i++) {
        VkImageView av[2] = { g->views[i], g->depth_view };
        VkFramebufferCreateInfo fi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fi.renderPass      = g->pass;
        fi.attachmentCount = 2;
        fi.pAttachments    = av;
        fi.width  = g->extent.width;
        fi.height = g->extent.height;
        fi.layers = 1;
        VKREQ(vkCreateFramebuffer(g->device, &fi, NULL, &g->fbs[i]), "vkCreateFramebuffer");
    }

    VkPushConstantRange pcr;
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size   = PUSH_SIZE;
    VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges    = &pcr;
    VKREQ(vkCreatePipelineLayout(g->device, &pl, NULL, &g->layout), "vkCreatePipelineLayout");

    VkShaderModuleCreateInfo smv = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smv.codeSize = sizeof(kMeshVert); smv.pCode = kMeshVert;
    VkShaderModule vs = VK_NULL_HANDLE;
    VKREQ(vkCreateShaderModule(g->device, &smv, NULL, &vs), "vkCreateShaderModule(vert)");
    VkShaderModuleCreateInfo smf = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    smf.codeSize = sizeof(kMeshFrag); smf.pCode = kMeshFrag;
    VkShaderModule fs = VK_NULL_HANDLE;
    VKREQ(vkCreateShaderModule(g->device, &smf, NULL, &fs), "vkCreateShaderModule(frag)");

    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

    VkVertexInputBindingDescription vb;
    vb.binding = 0; vb.stride = (uint32_t)sizeof(hta_vertex);
    vb.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription va[3];
    va[0].location = 0; va[0].binding = 0; va[0].format = VK_FORMAT_R32G32B32_SFLOAT; va[0].offset = 0;
    va[1].location = 1; va[1].binding = 0; va[1].format = VK_FORMAT_R32G32B32_SFLOAT; va[1].offset = 12;
    va[2].location = 2; va[2].binding = 0; va[2].format = VK_FORMAT_R32G32_SFLOAT;    va[2].offset = 24;

    VkPipelineVertexInputStateCreateInfo vin = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vin.vertexBindingDescriptionCount   = 1;
    vin.pVertexBindingDescriptions      = &vb;
    vin.vertexAttributeDescriptionCount = 3;
    vin.pVertexAttributeDescriptions    = va;

    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp = { 0, 0, (float)g->extent.width, (float)g->extent.height, 0.0f, 1.0f };
    VkRect2D   sc = { {0,0}, g->extent };
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = 1; vps.pViewports = &vp;
    vps.scissorCount  = 1; vps.pScissors  = &sc;

    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    /* Halo's winding is not yet confirmed; draw both faces so nothing silently
     * vanishes while we are still validating geometry extraction. */
    rs.cullMode  = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;
    ds.minDepthBounds   = 0.0f;
    ds.maxDepthBounds   = 1.0f;

    VkPipelineColorBlendAttachmentState cba;
    memset(&cba, 0, sizeof(cba));
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkGraphicsPipelineCreateInfo gp = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gp.stageCount          = 2;
    gp.pStages             = stages;
    gp.pVertexInputState   = &vin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState      = &vps;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pDepthStencilState  = &ds;
    gp.pColorBlendState    = &cb;
    gp.layout              = g->layout;
    gp.renderPass          = g->pass;
    VkResult pr = vkCreateGraphicsPipelines(g->device, VK_NULL_HANDLE, 1, &gp, NULL, &g->pipeline);
    vkDestroyShaderModule(g->device, vs, NULL);
    vkDestroyShaderModule(g->device, fs, NULL);
    if (pr != VK_SUCCESS) { gfail(err, errlen, "vkCreateGraphicsPipelines -> %d", (int)pr); return false; }
    return true;
}

static bool create_cmd_sync(hta_gfx *g, char *err, size_t errlen)
{
    VkCommandPoolCreateInfo pi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = g->qfamily;
    VKREQ(vkCreateCommandPool(g->device, &pi, NULL, &g->pool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool        = g->pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = g->image_count;
    VKREQ(vkAllocateCommandBuffers(g->device, &ai, g->cmd), "vkAllocateCommandBuffers");

    for (uint32_t i = 0; i < g->image_count; i++) {
        VkSemaphoreCreateInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VKREQ(vkCreateSemaphore(g->device, &si, NULL, &g->sem_acquire[i]), "vkCreateSemaphore");
        VKREQ(vkCreateSemaphore(g->device, &si, NULL, &g->sem_release[i]), "vkCreateSemaphore");
        VkFenceCreateInfo fi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VKREQ(vkCreateFence(g->device, &fi, NULL, &g->fence[i]), "vkCreateFence");
    }
    return true;
}

static hta_gfx *finish(hta_gfx *g, uint32_t w, uint32_t h, char *err, size_t errlen)
{
    if (!pick_device(g, err, errlen))            goto bad;
    if (!create_device(g, err, errlen))          goto bad;
    if (!create_targets(g, w, h, err, errlen))   goto bad;
    if (!create_pass_pipeline(g, err, errlen))   goto bad;
    if (!create_cmd_sync(g, err, errlen))        goto bad;
    g->ready = true;
    return g;
bad:
    hta_gfx_destroy(g);
    return NULL;
}

hta_gfx *hta_gfx_create_offscreen(uint32_t w, uint32_t h, char *err, size_t errlen)
{
    if (w == 0 || h == 0) { gfail(err, errlen, "offscreen size must be non-zero"); return NULL; }
    hta_gfx *g = (hta_gfx *)calloc(1, sizeof(*g));
    if (!g) { gfail(err, errlen, "out of memory"); return NULL; }
    g->offscreen = true;
    snprintf(g->device_name, sizeof(g->device_name), "%s", "(none)");
    if (!create_instance(g, false, err, errlen)) { hta_gfx_destroy(g); return NULL; }
    return finish(g, w, h, err, errlen);
}

hta_gfx *hta_gfx_create_window(void *native_window, char *err, size_t errlen)
{
    hta_gfx *g = (hta_gfx *)calloc(1, sizeof(*g));
    if (!g) { gfail(err, errlen, "out of memory"); return NULL; }
    g->window = native_window;
    snprintf(g->device_name, sizeof(g->device_name), "%s", "(none)");
    if (!create_instance(g, true, err, errlen)) { hta_gfx_destroy(g); return NULL; }
#if defined(__ANDROID__)
    VkAndroidSurfaceCreateInfoKHR si = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
    si.window = (struct ANativeWindow *)native_window;
    if (vkCreateAndroidSurfaceKHR(g->instance, &si, NULL, &g->surface) != VK_SUCCESS) {
        gfail(err, errlen, "vkCreateAndroidSurfaceKHR failed");
        hta_gfx_destroy(g);
        return NULL;
    }
#else
    gfail(err, errlen, "windowed mode is only implemented for Android");
    hta_gfx_destroy(g);
    return NULL;
#endif
    return finish(g, 1280, 720, err, errlen);
}

const char *hta_gfx_device_name(const hta_gfx *g) { return g ? g->device_name : "(none)"; }
uint64_t hta_gfx_device_memory_used(const hta_gfx *g) { return g ? g->mem_used : 0; }

void hta_gfx_extent(const hta_gfx *g, uint32_t *w, uint32_t *h)
{
    if (!g) { if (w) *w = 0; if (h) *h = 0; return; }
    if (w) *w = g->extent.width;
    if (h) *h = g->extent.height;
}

/* ------------------------------- meshes ------------------------------- */

static bool upload_via_staging(hta_gfx *g, VkBuffer dst, const void *src, VkDeviceSize size,
                               char *err, size_t errlen)
{
    VkBuffer sb = VK_NULL_HANDLE;
    VkDeviceMemory sm = VK_NULL_HANDLE;
    uint64_t before = g->mem_used;
    if (!make_buffer(g, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &sb, &sm, err, errlen)) return false;

    void *mapped = NULL;
    if (vkMapMemory(g->device, sm, 0, size, 0, &mapped) != VK_SUCCESS) {
        gfail(err, errlen, "vkMapMemory failed for staging buffer");
        vkDestroyBuffer(g->device, sb, NULL); vkFreeMemory(g->device, sm, NULL);
        return false;
    }
    memcpy(mapped, src, (size_t)size);
    vkUnmapMemory(g->device, sm);

    VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = g->pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    bool ok = false;
    if (vkAllocateCommandBuffers(g->device, &ai, &cb) == VK_SUCCESS) {
        VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        VkBufferCopy region = { 0, 0, size };
        vkCmdCopyBuffer(cb, sb, dst, 1, &region);
        vkEndCommandBuffer(cb);

        VkSubmitInfo su = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        su.commandBufferCount = 1;
        su.pCommandBuffers    = &cb;
        if (vkQueueSubmit(g->queue, 1, &su, VK_NULL_HANDLE) == VK_SUCCESS) {
            vkQueueWaitIdle(g->queue);
            ok = true;
        } else {
            gfail(err, errlen, "vkQueueSubmit failed during upload");
        }
        vkFreeCommandBuffers(g->device, g->pool, 1, &cb);
    } else {
        gfail(err, errlen, "vkAllocateCommandBuffers failed during upload");
    }

    vkDestroyBuffer(g->device, sb, NULL);
    vkFreeMemory(g->device, sm, NULL);
    g->mem_used = before;   /* staging was transient; don't count it */
    return ok;
}

hta_gfx_mesh *hta_gfx_mesh_upload(hta_gfx *g, const hta_vertex *verts, uint32_t nverts,
                                  const uint32_t *indices, uint32_t nindices,
                                  char *err, size_t errlen)
{
    if (!g || !g->ready || !verts || !indices || !nverts || !nindices) {
        gfail(err, errlen, "mesh upload: bad arguments");
        return NULL;
    }
    hta_gfx_mesh *m = (hta_gfx_mesh *)calloc(1, sizeof(*m));
    if (!m) { gfail(err, errlen, "out of memory"); return NULL; }

    VkDeviceSize vsz = (VkDeviceSize)nverts * sizeof(hta_vertex);
    VkDeviceSize isz = (VkDeviceSize)nindices * sizeof(uint32_t);

    if (!make_buffer(g, vsz, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m->vbuf, &m->vmem, err, errlen) ||
        !make_buffer(g, isz, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m->ibuf, &m->imem, err, errlen)) {
        hta_gfx_mesh_free(g, m);
        return NULL;
    }
    if (!upload_via_staging(g, m->vbuf, verts, vsz, err, errlen) ||
        !upload_via_staging(g, m->ibuf, indices, isz, err, errlen)) {
        hta_gfx_mesh_free(g, m);
        return NULL;
    }
    m->index_count = nindices;
    m->bytes = (uint64_t)vsz + (uint64_t)isz;
    return m;
}

void hta_gfx_mesh_free(hta_gfx *g, hta_gfx_mesh *m)
{
    if (!g || !m) return;
    if (g->device) {
        vkDeviceWaitIdle(g->device);
        if (m->vbuf) vkDestroyBuffer(g->device, m->vbuf, NULL);
        if (m->vmem) vkFreeMemory(g->device, m->vmem, NULL);
        if (m->ibuf) vkDestroyBuffer(g->device, m->ibuf, NULL);
        if (m->imem) vkFreeMemory(g->device, m->imem, NULL);
    }
    free(m);
}

/* -------------------------------- draw -------------------------------- */

static void fill_push(uint8_t *p, const hta_camera *cam, const hta_scene *s)
{
    hta_mat4 vp = hta_camera_view_proj(cam);
    memcpy(p + 0, vp.m, 64);
    float ld[4] = { s->light_dir[0], s->light_dir[1], s->light_dir[2], 0.0f };
    float lc[4] = { s->light_color[0], s->light_color[1], s->light_color[2], 0.0f };
    float am[4] = { s->ambient[0], s->ambient[1], s->ambient[2], 0.0f };
    memcpy(p + 64, ld, 16);
    memcpy(p + 80, lc, 16);
    memcpy(p + 96, am, 16);
}

bool hta_gfx_draw(hta_gfx *g, const hta_camera *cam, const hta_scene *scene,
                  hta_gfx_mesh *mesh)
{
    if (!g || !g->ready || !cam || !scene) return false;

    uint32_t slot = g->frame % g->image_count;
    vkWaitForFences(g->device, 1, &g->fence[slot], VK_TRUE, UINT64_MAX);

    uint32_t idx = 0;
    if (!g->offscreen) {
        VkResult r = vkAcquireNextImageKHR(g->device, g->swapchain, UINT64_MAX,
                                           g->sem_acquire[slot], VK_NULL_HANDLE, &idx);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) return false;
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;
    }
    vkResetFences(g->device, 1, &g->fence[slot]);

    VkCommandBuffer cb = g->cmd[slot];
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkClearValue clears[2];
    clears[0].color.float32[0] = scene->clear[0];
    clears[0].color.float32[1] = scene->clear[1];
    clears[0].color.float32[2] = scene->clear[2];
    clears[0].color.float32[3] = 1.0f;
    clears[1].depthStencil.depth   = 1.0f;
    clears[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo rb = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rb.renderPass      = g->pass;
    rb.framebuffer     = g->fbs[idx];
    rb.renderArea.offset.x = 0;
    rb.renderArea.offset.y = 0;
    rb.renderArea.extent   = g->extent;
    rb.clearValueCount = 2;
    rb.pClearValues    = clears;
    vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);

    if (mesh && mesh->index_count) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pipeline);
        uint8_t push[PUSH_SIZE];
        memset(push, 0, sizeof(push));
        fill_push(push, cam, scene);
        vkCmdPushConstants(cb, g->layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, PUSH_SIZE, push);
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &mesh->vbuf, &zero);
        vkCmdBindIndexBuffer(cb, mesh->ibuf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, mesh->index_count, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cb);

    if (g->offscreen) {
        VkBufferImageCopy region;
        memset(&region, 0, sizeof(region));
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width  = g->extent.width;
        region.imageExtent.height = g->extent.height;
        region.imageExtent.depth  = 1;
        vkCmdCopyImageToBuffer(cb, g->off_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               g->readback_buf, 1, &region);
    }
    vkEndCommandBuffer(cb);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo su = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    su.commandBufferCount = 1;
    su.pCommandBuffers    = &cb;
    if (!g->offscreen) {
        su.waitSemaphoreCount   = 1;
        su.pWaitSemaphores      = &g->sem_acquire[slot];
        su.pWaitDstStageMask    = &wait_stage;
        su.signalSemaphoreCount = 1;
        su.pSignalSemaphores    = &g->sem_release[slot];
    }
    if (vkQueueSubmit(g->queue, 1, &su, g->fence[slot]) != VK_SUCCESS) return false;

    if (g->offscreen) {
        vkWaitForFences(g->device, 1, &g->fence[slot], VK_TRUE, UINT64_MAX);
        g->frame++;
        return true;
    }

    VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &g->sem_release[slot];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &g->swapchain;
    pi.pImageIndices      = &idx;
    VkResult r = vkQueuePresentKHR(g->queue, &pi);
    g->frame++;
    return (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR);
}

bool hta_gfx_readback(hta_gfx *g, uint8_t *dst, size_t dst_size)
{
    if (!g || !g->offscreen || !dst) return false;
    size_t need = (size_t)g->extent.width * g->extent.height * 4u;
    if (dst_size < need) return false;

    void *mapped = NULL;
    if (vkMapMemory(g->device, g->readback_mem, 0, need, 0, &mapped) != VK_SUCCESS) return false;
    memcpy(dst, mapped, need);
    vkUnmapMemory(g->device, g->readback_mem);
    return true;
}

void hta_gfx_destroy(hta_gfx *g)
{
    if (!g) return;
    if (g->device) {
        vkDeviceWaitIdle(g->device);
        for (uint32_t i = 0; i < g->image_count; i++) {
            if (g->sem_acquire[i]) vkDestroySemaphore(g->device, g->sem_acquire[i], NULL);
            if (g->sem_release[i]) vkDestroySemaphore(g->device, g->sem_release[i], NULL);
            if (g->fence[i])       vkDestroyFence(g->device, g->fence[i], NULL);
            if (g->fbs[i])         vkDestroyFramebuffer(g->device, g->fbs[i], NULL);
            if (g->views[i])       vkDestroyImageView(g->device, g->views[i], NULL);
        }
        if (g->depth_view)   vkDestroyImageView(g->device, g->depth_view, NULL);
        if (g->depth_image)  vkDestroyImage(g->device, g->depth_image, NULL);
        if (g->depth_mem)    vkFreeMemory(g->device, g->depth_mem, NULL);
        if (g->readback_buf) vkDestroyBuffer(g->device, g->readback_buf, NULL);
        if (g->readback_mem) vkFreeMemory(g->device, g->readback_mem, NULL);
        if (g->off_image)    vkDestroyImage(g->device, g->off_image, NULL);
        if (g->off_mem)      vkFreeMemory(g->device, g->off_mem, NULL);
        if (g->pool)      vkDestroyCommandPool(g->device, g->pool, NULL);
        if (g->pipeline)  vkDestroyPipeline(g->device, g->pipeline, NULL);
        if (g->layout)    vkDestroyPipelineLayout(g->device, g->layout, NULL);
        if (g->pass)      vkDestroyRenderPass(g->device, g->pass, NULL);
        if (g->swapchain) vkDestroySwapchainKHR(g->device, g->swapchain, NULL);
        vkDestroyDevice(g->device, NULL);
    }
    if (g->surface)  vkDestroySurfaceKHR(g->instance, g->surface, NULL);
    if (g->instance) vkDestroyInstance(g->instance, NULL);
    free(g);
}
