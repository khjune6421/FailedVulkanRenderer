#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#else
import vulkan_hpp;
#endif

#include <vulkan/vulkan_raii.hpp>

#include <GLFW/glfw3.h>

#include <memory>
#include <iostream>
#include <cstdlib>
#include <fstream>

using namespace std;
using namespace vk;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

const vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION_LAYERS = false;
#else
constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif

class HelloTriangleApplication
{
	// Needs custom deleter because GLFW is a C library
	unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window{ nullptr, glfwDestroyWindow };

	raii::Context context{};
	raii::Instance instance = nullptr;
	raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
	raii::SurfaceKHR surface = nullptr;
	raii::PhysicalDevice physicalDevice = nullptr;

	raii::Device device = nullptr;
	vector<const char*> requiredDeviceExtension =
	{
		KHRSwapchainExtensionName,
		KHRSpirv14ExtensionName,
		KHRSynchronization2ExtensionName,
		KHRCreateRenderpass2ExtensionName
	};

	raii::Queue queue = nullptr;
	uint32_t queueIndex = ~0;

	raii::SwapchainKHR swapChain = nullptr;
	vector<Image> swapChainImages;
	SurfaceFormatKHR swapChainSurfaceFormat{};
	Extent2D swapChainExtent{};
	vector<raii::ImageView> swapChainImageViews;

	raii::PipelineLayout pipelineLayout = nullptr;
	raii::Pipeline graphicsPipeline = nullptr;

	raii::CommandPool commandPool = nullptr;
	vector<raii::CommandBuffer> commandBuffers;

	vector<raii::Semaphore> presentCompleteSemaphore;
	vector<raii::Semaphore> renderFinishedSemaphore;
	vector<raii::Fence> inFlightFences;
	uint32_t semaphoreIndex = 0;
	uint32_t currentFrame = 0;

	bool framebufferResized = false;

	void InitWindow()
	{
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window.reset(glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr));
		glfwSetWindowUserPointer(window.get(), this);
		glfwSetFramebufferSizeCallback(window.get(), FramebufferResizeCallback);
	}

	static void FramebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		auto app = reinterpret_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
		app->framebufferResized = true;
	}

	void InitVulkan()
	{
		CreateInstance();
		SetupDebugMessenger();
		CreateSurface();
		PickPhysicalDevice();
		CreateLogicalDevice();
		CreateSwapChain();
		CreateImageViews();
		CreateGraphicsPipeline();
		CreateCommandPool();
		CreateCommandBuffer();
		CreateSyncObjects();
	}

	void MainLoop()
	{
		while (!glfwWindowShouldClose(window.get()))
		{
			glfwPollEvents();
			DrawFrame();
		}

		device.waitIdle();
	}

	void CleanupSwapChain()
	{
		swapChainImageViews.clear();
		swapChain = nullptr;
	}

	void Cleanup()
	{
		glfwTerminate();
	}

	void RecreateSwapChain()
	{
		int width = 0, height = 0;
		glfwGetFramebufferSize(window.get(), &width, &height);

		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(window.get(), &width, &height);
			glfwWaitEvents();
		}

		device.waitIdle();

		CleanupSwapChain();
		CreateSwapChain();
		CreateImageViews();
	}

	void CreateInstance()
	{
		constexpr ApplicationInfo appInfo
		{
			"Hello Triangle",
			VK_MAKE_VERSION(1, 0, 0),
			"No Engine",
			VK_MAKE_VERSION(1, 0, 0),
			ApiVersion14
		};

		vector<const char*> requiredLayers;
		if (ENABLE_VALIDATION_LAYERS) requiredLayers.assign(validationLayers.begin(), validationLayers.end());

		vector<LayerProperties> layerProperties = context.enumerateInstanceLayerProperties();
		for (const char* requiredLayer : requiredLayers)
		{
			if (ranges::none_of(layerProperties, [requiredLayer](auto const& layerProperty) { return strcmp(layerProperty.layerName, requiredLayer) == 0; }))
			{
				throw runtime_error("Required layer not supported: " + string(requiredLayer));
			}
		}

		vector<const char*> requiredExtensions = GetRequiredExtensions();
		vector<ExtensionProperties> extensionProperties = context.enumerateInstanceExtensionProperties();
		for (const char* requiredExtension : requiredExtensions)
		{
			if (ranges::none_of(extensionProperties, [requiredExtension](auto const& extensionProperty) { return strcmp(extensionProperty.extensionName, requiredExtension) == 0; }))
			{
				throw runtime_error("Required extension not supported: " + string(requiredExtension));
			}
		}

		InstanceCreateInfo createInfo{};
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledLayerCount = static_cast<uint32_t>(requiredLayers.size());
		createInfo.ppEnabledLayerNames = requiredLayers.data();
		createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
		createInfo.ppEnabledExtensionNames = requiredExtensions.data();

		instance = raii::Instance{ context, createInfo };
	}

	void SetupDebugMessenger()
	{
		if (!ENABLE_VALIDATION_LAYERS) return;

		DebugUtilsMessageSeverityFlagsEXT severityFlags(DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | DebugUtilsMessageSeverityFlagBitsEXT::eWarning | DebugUtilsMessageSeverityFlagBitsEXT::eError);
		DebugUtilsMessageTypeFlagsEXT messageTypeFlags(DebugUtilsMessageTypeFlagBitsEXT::eGeneral | DebugUtilsMessageTypeFlagBitsEXT::ePerformance | DebugUtilsMessageTypeFlagBitsEXT::eValidation);

		DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{};
		debugUtilsMessengerCreateInfoEXT.messageSeverity = severityFlags;
		debugUtilsMessengerCreateInfoEXT.messageType = messageTypeFlags;
		debugUtilsMessengerCreateInfoEXT.pfnUserCallback = &DebugCallback;

		debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
	}

	void CreateSurface()
	{
		VkSurfaceKHR _surface = nullptr;
		if (glfwCreateWindowSurface(static_cast<VkInstance>(*instance), window.get(), nullptr, &_surface) != VK_SUCCESS)
		{
			throw runtime_error("failed to create window surface!");
		}
		surface = raii::SurfaceKHR{ instance, _surface };
	}

	void PickPhysicalDevice()
	{
		vector<raii::PhysicalDevice> devices = instance.enumeratePhysicalDevices();
		const auto devIter = ranges::find_if
		(
			devices,
			[&](raii::PhysicalDevice const& device)
			{
				bool supportsVulkan1_3 = device.getProperties().apiVersion >= VK_API_VERSION_1_3;

				vector<QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();
				bool supportsGraphics = ranges::any_of(queueFamilies, [](const QueueFamilyProperties& qfp) { return !!(qfp.queueFlags & QueueFlagBits::eGraphics); });

				vector<ExtensionProperties> availableDeviceExtensions = device.enumerateDeviceExtensionProperties();
				bool supportsAllRequiredExtensions = ranges::all_of
				(
					requiredDeviceExtension, [&availableDeviceExtensions](const char* requiredDeviceExtension)
					{
					return ranges::any_of
					(
						availableDeviceExtensions, [requiredDeviceExtension](const ExtensionProperties& availableDeviceExtension)
						{
							return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0;
						}
					);
					}
				);

				auto features = device.template getFeatures2<PhysicalDeviceFeatures2, PhysicalDeviceVulkan11Features, PhysicalDeviceVulkan13Features, PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
				bool supportsRequiredFeatures =
					features.template get<PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
					features.template get<PhysicalDeviceVulkan13Features>().synchronization2 &&
					features.template get<PhysicalDeviceVulkan13Features>().dynamicRendering &&
					features.template get<PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

				return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
			}
		);
		if (devIter != devices.end()) physicalDevice = *devIter;
		else throw runtime_error("failed to find a suitable GPU!");
	}

	void CreateLogicalDevice()
	{
		vector<QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

		for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
		{
			if ((queueFamilyProperties[qfpIndex].queueFlags & QueueFlagBits::eGraphics) && physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface))
			{
				queueIndex = qfpIndex;
				break;
			}
		}
		if (queueIndex == ~0) throw runtime_error("Could not find a queue for graphics and present -> terminating");

		PhysicalDeviceFeatures2 featureChain = {};

		PhysicalDeviceVulkan11Features vulkan11Features = {};
		vulkan11Features.shaderDrawParameters = true;

		PhysicalDeviceVulkan13Features vulkan13Features = {};
		vulkan13Features.synchronization2 = true;
		vulkan13Features.dynamicRendering = true;

		PhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicStateFeatures = {};
		extendedDynamicStateFeatures.extendedDynamicState = true;

		StructureChain
			<
			PhysicalDeviceFeatures2,
			PhysicalDeviceVulkan11Features,
			PhysicalDeviceVulkan13Features,
			PhysicalDeviceExtendedDynamicStateFeaturesEXT
			>
			featureStructureChain
		{
			featureChain,
			vulkan11Features,
			vulkan13Features,
			extendedDynamicStateFeatures
		};

		constexpr float queuePriority = 0.5f;
		DeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.queueFamilyIndex = queueIndex;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		DeviceCreateInfo createInfo = {};
		createInfo.pNext = &featureStructureChain.get<PhysicalDeviceFeatures2>();
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size());
		createInfo.ppEnabledExtensionNames = requiredDeviceExtension.data();

		device = raii::Device{ physicalDevice, createInfo };
		queue = raii::Queue{ device, queueIndex, 0 };
	}

	void CreateSwapChain()
	{
		SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
		swapChainExtent = ChooseSwapExtent(surfaceCapabilities);
		swapChainSurfaceFormat = ChooseSwapSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(*surface));

		SwapchainCreateInfoKHR swapChainCreateInfo{};
		swapChainCreateInfo.surface = *surface;
		swapChainCreateInfo.minImageCount = ChooseSwapMinImageCount(surfaceCapabilities);
		swapChainCreateInfo.imageFormat = swapChainSurfaceFormat.format;
		swapChainCreateInfo.imageColorSpace = swapChainSurfaceFormat.colorSpace;
		swapChainCreateInfo.imageExtent = swapChainExtent;
		swapChainCreateInfo.imageArrayLayers = 1;
		swapChainCreateInfo.imageUsage = ImageUsageFlagBits::eColorAttachment;
		swapChainCreateInfo.imageSharingMode = SharingMode::eExclusive;
		swapChainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
		swapChainCreateInfo.compositeAlpha = CompositeAlphaFlagBitsKHR::eOpaque;
		swapChainCreateInfo.presentMode = ChooseSwapPresentMode(physicalDevice.getSurfacePresentModesKHR(*surface));
		swapChainCreateInfo.clipped = true;

		swapChain = raii::SwapchainKHR{ device, swapChainCreateInfo };
		swapChainImages = swapChain.getImages();
	}

	void CreateImageViews()
	{
		assert(swapChainImageViews.empty());

		ImageViewCreateInfo imageViewCreateInfo{};
		imageViewCreateInfo.viewType = ImageViewType::e2D;
		imageViewCreateInfo.format = swapChainSurfaceFormat.format;
		imageViewCreateInfo.subresourceRange.aspectMask = ImageAspectFlagBits::eColor;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.levelCount = 1;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.layerCount = 1;

		for (const auto& image : swapChainImages)
		{
			imageViewCreateInfo.image = image;
			swapChainImageViews.emplace_back(device, imageViewCreateInfo);
		}
	}

	void CreateGraphicsPipeline()
	{
		raii::ShaderModule shaderModule = CreateShaderModule(ReadFile("Shader/Slang.spv"));

		PipelineShaderStageCreateInfo vertShaderStageInfo{};
		vertShaderStageInfo.stage = ShaderStageFlagBits::eVertex;
		vertShaderStageInfo.module = shaderModule;
		vertShaderStageInfo.pName = "vertMain";

		PipelineShaderStageCreateInfo fragShaderStageInfo{};
		fragShaderStageInfo.stage = ShaderStageFlagBits::eFragment;
		fragShaderStageInfo.module = shaderModule;
		fragShaderStageInfo.pName = "fragMain";

		array<PipelineShaderStageCreateInfo, 2> shaderStages = { vertShaderStageInfo, fragShaderStageInfo };

		PipelineVertexInputStateCreateInfo vertexInputInfo{};

		PipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.topology = PrimitiveTopology::eTriangleList;

		PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.depthClampEnable = False;
		rasterizer.rasterizerDiscardEnable = False;
		rasterizer.polygonMode = PolygonMode::eFill;
		rasterizer.cullMode = CullModeFlagBits::eBack;
		rasterizer.frontFace = FrontFace::eClockwise;
		rasterizer.depthBiasEnable = False;
		rasterizer.depthBiasSlopeFactor = 1.0f;
		rasterizer.lineWidth = 1.0f;

		PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = SampleCountFlagBits::e1;
		multisampling.sampleShadingEnable = False;

		PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.blendEnable = False;
		colorBlendAttachment.colorWriteMask =
			ColorComponentFlagBits::eR |
			ColorComponentFlagBits::eG |
			ColorComponentFlagBits::eB |
			ColorComponentFlagBits::eA;

		PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.logicOpEnable = False;
		colorBlending.logicOp = LogicOp::eCopy;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		vector dynamicStates = { DynamicState::eViewport, DynamicState::eScissor };

		PipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicState.pDynamicStates = dynamicStates.data();

		PipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.setLayoutCount = 0;
		pipelineLayoutInfo.pushConstantRangeCount = 0;

		pipelineLayout = raii::PipelineLayout{ device, pipelineLayoutInfo };

		GraphicsPipelineCreateInfo graphicsPipelineCreateInfo{};
		graphicsPipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		graphicsPipelineCreateInfo.pStages = shaderStages.data();
		graphicsPipelineCreateInfo.pVertexInputState = &vertexInputInfo;
		graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssembly;
		graphicsPipelineCreateInfo.pViewportState = &viewportState;
		graphicsPipelineCreateInfo.pRasterizationState = &rasterizer;
		graphicsPipelineCreateInfo.pMultisampleState = &multisampling;
		graphicsPipelineCreateInfo.pColorBlendState = &colorBlending;
		graphicsPipelineCreateInfo.pDynamicState = &dynamicState;
		graphicsPipelineCreateInfo.layout = pipelineLayout;

		PipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
		pipelineRenderingCreateInfo.colorAttachmentCount = 1;
		pipelineRenderingCreateInfo.pColorAttachmentFormats = &swapChainSurfaceFormat.format;

		StructureChain<GraphicsPipelineCreateInfo, PipelineRenderingCreateInfo> pipelineCreateInfoChain
		{
			graphicsPipelineCreateInfo,
			pipelineRenderingCreateInfo
		};

		graphicsPipeline = raii::Pipeline{ device, nullptr, pipelineCreateInfoChain.get<GraphicsPipelineCreateInfo>() };
	}

	void CreateCommandPool()
	{
		CommandPoolCreateInfo poolInfo{};
		poolInfo.flags = CommandPoolCreateFlagBits::eResetCommandBuffer;
		poolInfo.queueFamilyIndex = queueIndex;

		commandPool = raii::CommandPool{ device, poolInfo };
	}

	void CreateCommandBuffer()
	{
		CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = commandPool;
		allocInfo.level = CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

		commandBuffers = raii::CommandBuffers{ device, allocInfo };
	}

	void RecordCommandBuffer(uint32_t imageIndex)
	{
		commandBuffers[currentFrame].begin(CommandBufferBeginInfo{});

		TransitionImageLayout
		(
			imageIndex,
			ImageLayout::eUndefined,
			ImageLayout::eColorAttachmentOptimal,
			{},
			AccessFlagBits2::eColorAttachmentWrite,
			PipelineStageFlagBits2::eTopOfPipe,
			PipelineStageFlagBits2::eColorAttachmentOutput
		);

		const ClearValue clearColor = ClearColorValue{ array<float, 4>{ 0.2f, 0.2f, 0.2f, 1.0f } };

		RenderingAttachmentInfo colorAttachmentInfo{};
		colorAttachmentInfo.imageView = *swapChainImageViews[imageIndex];
		colorAttachmentInfo.imageLayout = ImageLayout::eColorAttachmentOptimal;
		colorAttachmentInfo.loadOp = AttachmentLoadOp::eClear;
		colorAttachmentInfo.storeOp = AttachmentStoreOp::eStore;
		colorAttachmentInfo.clearValue = clearColor;

		RenderingInfo renderingInfo{};
		renderingInfo.renderArea.offset = Offset2D{ 0, 0 };
		renderingInfo.renderArea.extent = swapChainExtent;
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachmentInfo;

		commandBuffers[currentFrame].beginRendering(renderingInfo);

		commandBuffers[currentFrame].bindPipeline(PipelineBindPoint::eGraphics, *graphicsPipeline);
		commandBuffers[currentFrame].setViewport(0, Viewport{ 0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f });
		commandBuffers[currentFrame].setScissor(0, Rect2D{ Offset2D{ 0, 0 }, swapChainExtent });
		commandBuffers[currentFrame].draw(3, 1, 0, 0);

		commandBuffers[currentFrame].endRendering();

		TransitionImageLayout
		(
			imageIndex,
			ImageLayout::eColorAttachmentOptimal,
			ImageLayout::ePresentSrcKHR,
			AccessFlagBits2::eColorAttachmentWrite,
			{},
			PipelineStageFlagBits2::eColorAttachmentOutput,
			PipelineStageFlagBits2::eBottomOfPipe
		);

		commandBuffers[currentFrame].end();
	}

	void TransitionImageLayout
	(
		uint32_t imageIndex,
		ImageLayout oldLayout,
		ImageLayout newLayout,
		AccessFlags2 srcAccessMask,
		AccessFlags2 dstAccessMask,
		PipelineStageFlags2 srcStageMask,
		PipelineStageFlags2 dstStageMask
	)
	{
		ImageMemoryBarrier2 barrier{};
		barrier.srcStageMask = srcStageMask;
		barrier.srcAccessMask = srcAccessMask;
		barrier.dstStageMask = dstStageMask;
		barrier.dstAccessMask = dstAccessMask;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = swapChainImages[imageIndex];
		barrier.subresourceRange.aspectMask = ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		DependencyInfo dependencyInfo{};
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &barrier;
		commandBuffers[currentFrame].pipelineBarrier2(dependencyInfo);
	}

	void CreateSyncObjects()
	{
		presentCompleteSemaphore.clear();
		renderFinishedSemaphore.clear();
		inFlightFences.clear();

		for (size_t i = 0; i < swapChainImages.size(); i++)
		{
			presentCompleteSemaphore.emplace_back(device, SemaphoreCreateInfo{});
			renderFinishedSemaphore.emplace_back(device, SemaphoreCreateInfo{});
		}

		FenceCreateInfo fenceInfo{};
		fenceInfo.flags = FenceCreateFlagBits::eSignaled;

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) inFlightFences.emplace_back(device, fenceInfo);
	}

	void DrawFrame()
	{
		while (device.waitForFences(*inFlightFences[currentFrame], True, UINT64_MAX) == Result::eTimeout);

		auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphore[semaphoreIndex], nullptr);

		if (result == Result::eErrorOutOfDateKHR) { RecreateSwapChain(); return; }
		else if (result != Result::eSuccess && result != Result::eSuboptimalKHR) throw runtime_error("failed to acquire swap chain image!");

		device.resetFences(*inFlightFences[currentFrame]);
		commandBuffers[currentFrame].reset();
		RecordCommandBuffer(imageIndex);

		PipelineStageFlags waitDestinationStageMask = PipelineStageFlagBits::eColorAttachmentOutput;

		SubmitInfo submitInfo{};
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &*presentCompleteSemaphore[semaphoreIndex];
		submitInfo.pWaitDstStageMask = &waitDestinationStageMask;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*commandBuffers[currentFrame];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &*renderFinishedSemaphore[imageIndex];
		queue.submit(submitInfo, *inFlightFences[currentFrame]);

		PresentInfoKHR presentInfoKHR{};
		presentInfoKHR.waitSemaphoreCount = 1;
		presentInfoKHR.pWaitSemaphores = &*renderFinishedSemaphore[imageIndex];
		presentInfoKHR.swapchainCount = 1;
		presentInfoKHR.pSwapchains = &*swapChain;
		presentInfoKHR.pImageIndices = &imageIndex;
		
		try
		{
			result = queue.presentKHR(presentInfoKHR);

			if (result == Result::eErrorOutOfDateKHR || result == Result::eSuboptimalKHR || framebufferResized)
			{
				framebufferResized = false;
				RecreateSwapChain();
			}
			else if (result != Result::eSuccess) throw runtime_error("failed to present swap chain image!");
		}
		catch (const SystemError& e)
		{
			if (e.code().value() == static_cast<int>(Result::eErrorOutOfDateKHR))
			{
				RecreateSwapChain();
				return;
			}
			else throw;
		}

		semaphoreIndex = (semaphoreIndex + 1) % presentCompleteSemaphore.size();
		currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	[[nodiscard]] raii::ShaderModule CreateShaderModule(const vector<char>& code) const
	{
		ShaderModuleCreateInfo createInfo{};
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		raii::ShaderModule shaderModule{ device, createInfo };

		return shaderModule;
	}

	static uint32_t ChooseSwapMinImageCount(SurfaceCapabilitiesKHR const& surfaceCapabilities)
	{
		uint32_t minImageCount = max(3u, surfaceCapabilities.minImageCount);
		if (surfaceCapabilities.maxImageCount && (surfaceCapabilities.maxImageCount < minImageCount)) minImageCount = surfaceCapabilities.maxImageCount;

		return minImageCount;
	}

	static SurfaceFormatKHR ChooseSwapSurfaceFormat(vector<SurfaceFormatKHR> const& availableFormats)
	{
		assert(!availableFormats.empty());
		const auto formatIt = ranges::find_if(availableFormats, [](const auto& format) { return format.format == Format::eB8G8R8A8Srgb && format.colorSpace == ColorSpaceKHR::eSrgbNonlinear; });

		return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
	}

	static PresentModeKHR ChooseSwapPresentMode(const vector<PresentModeKHR>& availablePresentModes)
	{
		assert(ranges::any_of(availablePresentModes, [](auto presentMode) { return presentMode == PresentModeKHR::eFifo; }));

		return ranges::any_of(availablePresentModes, [](const PresentModeKHR value) { return PresentModeKHR::eMailbox == value; }) ? PresentModeKHR::eMailbox : PresentModeKHR::eFifo;
	}

	Extent2D ChooseSwapExtent(const SurfaceCapabilitiesKHR& capabilities)
	{
		if (capabilities.currentExtent.width != 0xFFFFFFFF) return capabilities.currentExtent;

		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(window.get(), &width, &height);

		return
		{
			clamp<uint32_t>(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			clamp<uint32_t>(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
		};
	}

	vector<const char*> GetRequiredExtensions()
	{
		uint32_t glfwExtensionCount = 0;

		const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
		vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

		if (ENABLE_VALIDATION_LAYERS) extensions.push_back(EXTDebugUtilsExtensionName);

		return extensions;
	}

	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback
	(
		DebugUtilsMessageSeverityFlagBitsEXT severity,
		DebugUtilsMessageTypeFlagsEXT type,
		const DebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* /*pUserData*/
	)
	{
		if (severity & (DebugUtilsMessageSeverityFlagBitsEXT::eError | DebugUtilsMessageSeverityFlagBitsEXT::eWarning))
		{
			cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << endl;
		}

		return False;
	}

	static vector<char> ReadFile(const string& filename)
	{
		ifstream file(filename, ios::ate | ios::binary);
		if (!file.is_open()) throw runtime_error("failed to open file: " + filename);

		vector<char> buffer(file.tellg());
		file.seekg(0, ios::beg);
		file.read(buffer.data(), static_cast<streamsize>(buffer.size()));
		file.close();

		return buffer;
	}

public:
	void Run()
	{
		InitWindow();
		InitVulkan();
		MainLoop();
		Cleanup();
	}
};

int main()
{
	HelloTriangleApplication app;

	try { app.Run(); }
	catch (const exception& e)
	{
		cerr << e.what() << endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}