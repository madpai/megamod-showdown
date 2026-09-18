/* Minimal Vulkan backend for the Stage-1 proof of concept.
 * Goal: prove instance/surface/device/swapchain/pipeline/present all work on a
 * real ARM64 Android device, and draw something driven by engine state. */
#include "gfx.h"
#include "../platform/platform.h"

#include <stdlib.h>
#include <string.h>

#if defined(__ANDROID__)
#  define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#include <vulkan/vulkan.h>

static const uint32_t kTriVert[] = {
#include "../../shaders/tri_vert.inl"
};
static const uint32_t kTriFrag[] = {
#include "../../shaders/tri_frag.inl"
};

#define MAX_IMAGES 8

struct hta_gfx {
    VkInstance        instance;
    VkSurfaceKHR      surface;
    VkPhysicalDevice  phys;
    VkDevice          device;
    uint32_t          qfamily;
    VkQueue           queue;

    VkSwapchainKHR    swapchain;
    VkFormat          format;
    VkExtent2D        extent;
    uint32_t          image_count;
    VkImage           images[MAX_IMAGES];
    VkImageView       views[MAX_IMAGES];
    VkFramebuffer     fbs[MAX_IMAGES];

    VkRenderPass      pass;
    VkPipelineLayout  layout;
    VkPipeline        pipeline;
    VkCommandPool     pool;
    VkCommandBuffer   cmd[MAX_IMAGES];

    VkSemaphore       sem_acquire[MAX_IMAGES];
    VkSemaphore       sem_release[MAX_IMAGES];
    VkFence           fence[MAX_IMAGES];
    uint32_t          frame;

    void             *window;
    char              device_name[256];
    bool              ready;
};

#define VKCHECK(expr, what) do { \
    VkResult _r = (expr); \
    if (_r != VK_SUCCESS) { hta_log("[gfx] FAIL %s -> VkResult %d", what, (int)_r); return false; } \
} while (0)

static VkShaderModule make_module(VkDevice d, const uint32_t *code, size_t bytes)
{
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = bytes;
    ci.pCode    = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(d, &ci, NULL, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

static bool create_instance_and_surface(hta_gfx *g)
{
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "halo-trial-android";
    app.apiVersion       = VK_API_VERSION_1_1;

    const char *exts[2];
    uint32_t next = 0;
    exts[next++] = VK_KHR_SURFACE_EXTENSION_NAME;
#if defined(__ANDROID__)
    exts[next++] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
#endif

    VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = next;
    ci.ppEnabledExtensionNames = exts;
    VKCHECK(vkCreateInstance(&ci, NULL, &g->instance), "vkCreateInstance");

#if defined(__ANDROID__)
    VkAndroidSurfaceCreateInfoKHR si = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
    si.window = (struct ANativeWindow *)g->window;
    VKCHECK(vkCreateAndroidSurfaceKHR(g->instance, &si, NULL, &g->surface), "vkCreateAndroidSurfaceKHR");
#endif
    return true;
}

static bool pick_device(hta_gfx *g)
{
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(g->instance, &n, NULL);
    if (n == 0) { hta_log("[gfx] no Vulkan physical devices"); return false; }
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
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], q, g->surface, &present);
            if ((qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                g->phys = devs[i];
                g->qfamily = q;
                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(devs[i], &props);
                snprintf(g->device_name, sizeof(g->device_name), "%s", props.deviceName);
                hta_log("[gfx] device: %s (queue family %u)", props.deviceName, q);
                return true;
            }
        }
    }
    hta_log("[gfx] no graphics+present queue found");
    return false;
}

static bool create_device(hta_gfx *g)
{
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = g->qfamily;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    const char *dexts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo ci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = dexts;
    VKCHECK(vkCreateDevice(g->phys, &ci, NULL, &g->device), "vkCreateDevice");
    vkGetDeviceQueue(g->device, g->qfamily, 0, &g->queue);
    return true;
}

static bool create_swapchain(hta_gfx *g)
{
    VkSurfaceCapabilitiesKHR caps;
    VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g->phys, g->surface, &caps),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g->phys, g->surface, &fn, NULL);
    if (fn == 0) return false;
    if (fn > 32) fn = 32;
    VkSurfaceFormatKHR fmts[32];
    vkGetPhysicalDeviceSurfaceFormatsKHR(g->phys, g->surface, &fn, fmts);
    VkSurfaceFormatKHR chosen = fmts[0];
    for (uint32_t i = 0; i < fn; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM || fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            chosen = fmts[i];
            break;
        }
    }
    g->format = chosen.format;
    g->extent = caps.currentExtent;
    if (g->extent.width == 0xFFFFFFFFu) { g->extent.width = 1080; g->extent.height = 2340; }
    if (g->extent.width == 0 || g->extent.height == 0) return false;

    uint32_t want = caps.minImageCount + 1;
    if (caps.maxImageCount && want > caps.maxImageCount) want = caps.maxImageCount;
    if (want > MAX_IMAGES) want = MAX_IMAGES;

    VkSwapchainCreateInfoKHR ci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    ci.surface          = g->surface;
    ci.minImageCount    = want;
    ci.imageFormat      = chosen.format;
    ci.imageColorSpace  = chosen.colorSpace;
    ci.imageExtent      = g->extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;   /* always supported */
    ci.clipped          = VK_TRUE;
    VKCHECK(vkCreateSwapchainKHR(g->device, &ci, NULL, &g->swapchain), "vkCreateSwapchainKHR");

    g->image_count = MAX_IMAGES;
    VKCHECK(vkGetSwapchainImagesKHR(g->device, g->swapchain, &g->image_count, g->images),
            "vkGetSwapchainImagesKHR");
    hta_log("[gfx] swapchain %ux%u, %u images, format %d",
            g->extent.width, g->extent.height, g->image_count, (int)g->format);
    return true;
}

static bool create_pass_and_pipeline(hta_gfx *g)
{
    VkAttachmentDescription at = {0};
    at.format         = g->format;
    at.samples        = VK_SAMPLE_COUNT_1_BIT;
    at.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    at.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    at.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    at.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments    = &ref;

    VkSubpassDependency dep = {0};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rp.attachmentCount = 1; rp.pAttachments = &at;
    rp.subpassCount    = 1; rp.pSubpasses   = &sub;
    rp.dependencyCount = 1; rp.pDependencies = &dep;
    VKCHECK(vkCreateRenderPass(g->device, &rp, NULL, &g->pass), "vkCreateRenderPass");

    for (uint32_t i = 0; i < g->image_count; i++) {
        VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image    = g->images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format   = g->format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        VKCHECK(vkCreateImageView(g->device, &vi, NULL, &g->views[i]), "vkCreateImageView");

        VkFramebufferCreateInfo fi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fi.renderPass      = g->pass;
        fi.attachmentCount = 1;
        fi.pAttachments    = &g->views[i];
        fi.width  = g->extent.width;
        fi.height = g->extent.height;
        fi.layers = 1;
        VKCHECK(vkCreateFramebuffer(g->device, &fi, NULL, &g->fbs[i]), "vkCreateFramebuffer");
    }

    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) };
    VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges    = &pcr;
    VKCHECK(vkCreatePipelineLayout(g->device, &pl, NULL, &g->layout), "vkCreatePipelineLayout");

    VkShaderModule vs = make_module(g->device, kTriVert, sizeof(kTriVert));
    VkShaderModule fs = make_module(g->device, kTriFrag, sizeof(kTriFrag));
    if (!vs || !fs) { hta_log("[gfx] shader module creation failed"); return false; }

    VkPipelineShaderStageCreateInfo stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
    };
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vs; stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vin = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp = { 0, 0, (float)g->extent.width, (float)g->extent.height, 0.0f, 1.0f };
    VkRect2D   sc = { {0,0}, g->extent };
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vps.viewportCount = 1; vps.pViewports = &vp;
    vps.scissorCount  = 1; vps.pScissors  = &sc;

    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba = {0};
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
    gp.pColorBlendState    = &cb;
    gp.layout              = g->layout;
    gp.renderPass          = g->pass;
    VkResult pr = vkCreateGraphicsPipelines(g->device, VK_NULL_HANDLE, 1, &gp, NULL, &g->pipeline);
    vkDestroyShaderModule(g->device, vs, NULL);
    vkDestroyShaderModule(g->device, fs, NULL);
    if (pr != VK_SUCCESS) { hta_log("[gfx] vkCreateGraphicsPipelines -> %d", (int)pr); return false; }
    return true;
}

static bool create_commands_and_sync(hta_gfx *g)
{
    VkCommandPoolCreateInfo pi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pi.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = g->qfamily;
    VKCHECK(vkCreateCommandPool(g->device, &pi, NULL, &g->pool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool        = g->pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = g->image_count;
    VKCHECK(vkAllocateCommandBuffers(g->device, &ai, g->cmd), "vkAllocateCommandBuffers");

    for (uint32_t i = 0; i < g->image_count; i++) {
        VkSemaphoreCreateInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VKCHECK(vkCreateSemaphore(g->device, &si, NULL, &g->sem_acquire[i]), "sem acquire");
        VKCHECK(vkCreateSemaphore(g->device, &si, NULL, &g->sem_release[i]), "sem release");
        VkFenceCreateInfo fi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VKCHECK(vkCreateFence(g->device, &fi, NULL, &g->fence[i]), "fence");
    }
    return true;
}

hta_gfx *hta_gfx_create(void *native_window)
{
    hta_gfx *g = (hta_gfx *)calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->window = native_window;
    snprintf(g->device_name, sizeof(g->device_name), "%s", "(unknown)");

    if (!create_instance_and_surface(g)) goto fail;
    if (!pick_device(g))                 goto fail;
    if (!create_device(g))               goto fail;
    if (!create_swapchain(g))            goto fail;
    if (!create_pass_and_pipeline(g))    goto fail;
    if (!create_commands_and_sync(g))    goto fail;

    g->ready = true;
    hta_log("[gfx] Vulkan initialized OK");
    return g;
fail:
    hta_log("[gfx] Vulkan initialization FAILED");
    hta_gfx_destroy(g);
    return NULL;
}

const char *hta_gfx_device_name(const hta_gfx *g)
{
    return g ? g->device_name : "(none)";
}

bool hta_gfx_draw(hta_gfx *g, const hta_engine *e)
{
    if (!g || !g->ready) return false;

    uint32_t slot = g->frame % g->image_count;
    vkWaitForFences(g->device, 1, &g->fence[slot], VK_TRUE, UINT64_MAX);

    uint32_t idx = 0;
    VkResult r = vkAcquireNextImageKHR(g->device, g->swapchain, UINT64_MAX,
                                       g->sem_acquire[slot], VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { hta_log("[gfx] swapchain out of date"); return false; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { hta_log("[gfx] acquire -> %d", (int)r); return false; }

    vkResetFences(g->device, 1, &g->fence[slot]);

    VkCommandBuffer cb = g->cmd[slot];
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    /* clear colour comes straight from engine state -> proves engine drives GPU */
    VkClearValue clear;
    clear.color.float32[0] = e->clear_r;
    clear.color.float32[1] = e->clear_g;
    clear.color.float32[2] = e->clear_b;
    clear.color.float32[3] = 1.0f;

    VkRenderPassBeginInfo rb = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rb.renderPass      = g->pass;
    rb.framebuffer     = g->fbs[idx];
    rb.renderArea      = (VkRect2D){ {0,0}, g->extent };
    rb.clearValueCount = 1;
    rb.pClearValues    = &clear;
    vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pipeline);
    float spin = e->tri_spin;
    vkCmdPushConstants(cb, g->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &spin);
    vkCmdDraw(cb, 3, 1, 0, 0);

    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo su = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    su.waitSemaphoreCount   = 1;
    su.pWaitSemaphores      = &g->sem_acquire[slot];
    su.pWaitDstStageMask    = &wait_stage;
    su.commandBufferCount   = 1;
    su.pCommandBuffers      = &cb;
    su.signalSemaphoreCount = 1;
    su.pSignalSemaphores    = &g->sem_release[slot];
    if (vkQueueSubmit(g->queue, 1, &su, g->fence[slot]) != VK_SUCCESS) return false;

    VkPresentInfoKHR pr = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pr.waitSemaphoreCount = 1;
    pr.pWaitSemaphores    = &g->sem_release[slot];
    pr.swapchainCount     = 1;
    pr.pSwapchains        = &g->swapchain;
    pr.pImageIndices      = &idx;
    r = vkQueuePresentKHR(g->queue, &pr);
    g->frame++;
    return (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR);
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
