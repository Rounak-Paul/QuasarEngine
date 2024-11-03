#include "VulkanImage.h"

namespace Quasar::Vulkan
{
VkImageSubresourceRange image_subresource_range(VkImageAspectFlags aspect_mask);

void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout current_layout, VkImageLayout new_layout)
{
    VkImageMemoryBarrier image_barrier{};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.pNext = nullptr;

    // Set source and destination pipeline stages and access masks
    image_barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

    image_barrier.oldLayout = current_layout;
    image_barrier.newLayout = new_layout;

    VkImageAspectFlags aspect_mask = (new_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) 
                                     ? VK_IMAGE_ASPECT_DEPTH_BIT 
                                     : VK_IMAGE_ASPECT_COLOR_BIT;

    // Set the subresource range with the aspect mask
    image_barrier.subresourceRange.aspectMask = aspect_mask;
    image_barrier.subresourceRange.baseMipLevel = 0;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.baseArrayLayer = 0;
    image_barrier.subresourceRange.layerCount = 1;
    image_barrier.image = image;

    // Define source and destination pipeline stages
    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    // Execute the barrier
    vkCmdPipelineBarrier(
        cmd,
        src_stage,
        dst_stage,
        0,
        0, nullptr,
        0, nullptr,
        1, &image_barrier
    );
}

void copy_image_to_image(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize)
{
	VkImageBlit blitRegion{};
	blitRegion.srcOffsets[0] = {0, 0, 0};
	blitRegion.srcOffsets[1] = {static_cast<int32_t>(srcSize.width), static_cast<int32_t>(srcSize.height), 1};

	blitRegion.dstOffsets[0] = {0, 0, 0};
	blitRegion.dstOffsets[1] = {static_cast<int32_t>(dstSize.width), static_cast<int32_t>(dstSize.height), 1};

	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcSubresource.mipLevel = 0;

	blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;
	blitRegion.dstSubresource.mipLevel = 0;

	vkCmdBlitImage(
		cmd,
		source,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		destination,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&blitRegion,
		VK_FILTER_LINEAR
	);

}

} // namespace Quasar::Vulkan
