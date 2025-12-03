#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#else
import vulkan_hpp;
#endif

#include <vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <memory>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <chrono>

using namespace std;
using namespace vk;

struct Vertex
{
	glm::vec3 pos;
	glm::vec3 col;
	glm::vec2 UV;

	static VertexInputBindingDescription getBindingDescription() { return { 0, sizeof(Vertex), VertexInputRate::eVertex }; }
	static array<VertexInputAttributeDescription, 3> getAttributeDescriptions()
	{
		return
		{
			VertexInputAttributeDescription(0, 0, Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
			VertexInputAttributeDescription(1, 0, Format::eR32G32B32Sfloat, offsetof(Vertex, col)),
			VertexInputAttributeDescription(2, 0, Format::eR32G32Sfloat, offsetof(Vertex, UV))
		};
	}
};

struct MatrixUB
{
	glm::mat4 world;
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 WVP;
};

const vector<Vertex> vertices =
{
	{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } },
	{ {  0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
	{ {  0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
	{ { -0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },

	{ { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } },
	{ {  0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
	{ {  0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
	{ { -0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } }
};
const vector<uint16_t> indices =
{
	0, 1, 2,
	2, 3, 0,

	4, 5, 6,
	6, 7, 4
};

class HelloTriangleApplication
{
	int m_width = 800;
	int m_height = 600;
	const int MAX_FRAMES_IN_FLIGHT = 2;

	const vector<const char*> VALIDATION_LAYERS = { "VK_LAYER_KHRONOS_validation" };
#ifdef NDEBUG
	const bool ENABLE_VALIDATION_LAYERS = false;
#else
	const bool ENABLE_VALIDATION_LAYERS = true;
#endif

	// Needs custom deleter because GLFW is a C library // atleast if you want this to be raii compliant
	unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> m_window{ nullptr, glfwDestroyWindow };

	raii::Context m_context{};
	raii::Instance m_instance = nullptr;
	raii::DebugUtilsMessengerEXT m_debugMessenger = nullptr;
	raii::SurfaceKHR m_surface = nullptr;
	raii::PhysicalDevice m_physicalDevice = nullptr;

	raii::Device m_device = nullptr;
	vector<const char*> m_requiredDeviceExtension =
	{
		KHRSwapchainExtensionName,
		KHRSpirv14ExtensionName,
		KHRSynchronization2ExtensionName,
		KHRCreateRenderpass2ExtensionName
	};

	raii::Queue m_queue = nullptr;
	uint32_t m_queueIndex = ~0;

	raii::SwapchainKHR m_swapChain = nullptr;
	vector<Image> m_swapChainImages;
	SurfaceFormatKHR m_swapChainSurfaceFormat{};
	Extent2D m_swapChainExtent{};
	vector<raii::ImageView> m_swapChainImageViews;

	raii::DescriptorSetLayout m_descriptorSetLayout = nullptr;

	raii::RenderPass m_renderPass = nullptr;

	raii::PipelineLayout m_pipelineLayout = nullptr;
	raii::Pipeline m_graphicsPipeline = nullptr;

	raii::Image m_depthImage = nullptr;
	raii::DeviceMemory m_depthImageMemory = nullptr;
	raii::ImageView m_depthImageView = nullptr;

	raii::Image m_textureImage = nullptr;
	raii::DeviceMemory m_textureImageMemory = nullptr;
	raii::ImageView m_textureImageView = nullptr;
	raii::Sampler m_textureSampler = nullptr;

	raii::Buffer m_vertexBuffer = nullptr;
	raii::DeviceMemory m_vertexBufferMemory = nullptr;
	raii::Buffer m_indexBuffer = nullptr;
	raii::DeviceMemory m_indexBufferMemory = nullptr;

	vector<raii::Buffer> m_uniformBuffers;
	vector<raii::DeviceMemory> m_uniformBuffersMemory;
	vector<void*> m_uniformBuffersMapped;

	raii::DescriptorPool m_descriptorPool = nullptr;
	vector<raii::DescriptorSet> m_descriptorSets;

	raii::CommandPool m_commandPool = nullptr;
	vector<raii::CommandBuffer> m_commandBuffers;

	vector<raii::Semaphore> m_presentCompleteSemaphore;
	vector<raii::Semaphore> m_renderFinishedSemaphore;
	vector<raii::Fence> m_inFlightFences;
	uint32_t m_semaphoreIndex = 0;
	uint32_t m_currentFrame = 0;

	bool m_framebufferResized = false;

	void InitWindow()
	{
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

		m_window.reset(glfwCreateWindow(m_width, m_height, "Vulkan", nullptr, nullptr));
		glfwSetWindowUserPointer(m_window.get(), this);
		glfwSetFramebufferSizeCallback(m_window.get(), FramebufferResizeCallback);
	}

	static void FramebufferResizeCallback(GLFWwindow* window, int width, int height)
	{
		auto app = static_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));

		app->m_width = width;
		app->m_height = height;

		app->m_framebufferResized = true;
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
		CreateDescriptorSetLayout();
		CreateGraphicsPipeline();
		CreateCommandPool();
		CreateDepthResources();
		CreateTextureImage();
		CreateTextureImageView();
		CreateTextureSampler();
		CreateVertexBuffer();
		CreateIndexBuffer();
		CreateUniformBuffers();
		CreateDescriptorPool();
		CreateDescriptorSets();
		CreateCommandBuffer();
		CreateSyncObjects();
	}

	void MainLoop()
	{
		while (!glfwWindowShouldClose(m_window.get()))
		{
			glfwPollEvents();
			DrawFrame();
		}

		m_device.waitIdle();
	}

	void CleanupSwapChain()
	{
		m_swapChainImageViews.clear();
		m_swapChain = nullptr;
	}

	void Cleanup()
	{
		glfwTerminate();
	}

	void RecreateSwapChain()
	{
		glfwGetFramebufferSize(m_window.get(), &m_width, &m_height);

		while (m_width == 0 || m_height == 0)
		{
			glfwGetFramebufferSize(m_window.get(), &m_width, &m_height);
			glfwWaitEvents();
		}

		m_device.waitIdle();

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
		if (ENABLE_VALIDATION_LAYERS) requiredLayers.assign(VALIDATION_LAYERS.begin(), VALIDATION_LAYERS.end());

		vector<LayerProperties> layerProperties = m_context.enumerateInstanceLayerProperties();
		for (const char* requiredLayer : requiredLayers)
		{
			if (ranges::none_of(layerProperties, [requiredLayer](auto const& layerProperty) { return strcmp(layerProperty.layerName, requiredLayer) == 0; }))
			{
				throw runtime_error("Required layer not supported: " + string(requiredLayer));
			}
		}

		vector<const char*> requiredExtensions = GetRequiredExtensions();
		vector<ExtensionProperties> extensionProperties = m_context.enumerateInstanceExtensionProperties();
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

		m_instance = raii::Instance{ m_context, createInfo };
	}

	void SetupDebugMessenger()
	{
		if (!ENABLE_VALIDATION_LAYERS) return;

		DebugUtilsMessageSeverityFlagsEXT severityFlags
		(
			DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
			DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
			DebugUtilsMessageSeverityFlagBitsEXT::eError
		);
		DebugUtilsMessageTypeFlagsEXT messageTypeFlags
		(
			DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
			DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
			DebugUtilsMessageTypeFlagBitsEXT::eValidation
		);

		DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{};
		debugUtilsMessengerCreateInfoEXT.messageSeverity = severityFlags;
		debugUtilsMessengerCreateInfoEXT.messageType = messageTypeFlags;
		debugUtilsMessengerCreateInfoEXT.pfnUserCallback = &DebugCallback;

		m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
	}

	void CreateSurface()
	{
		VkSurfaceKHR _surface = nullptr;
		if (glfwCreateWindowSurface(static_cast<VkInstance>(*m_instance), m_window.get(), nullptr, &_surface) != VK_SUCCESS)
		{
			throw runtime_error("failed to create window surface!");
		}
		m_surface = raii::SurfaceKHR{ m_instance, _surface };
	}

	void PickPhysicalDevice()
	{
		vector<raii::PhysicalDevice> devices = m_instance.enumeratePhysicalDevices();
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
					m_requiredDeviceExtension, [&availableDeviceExtensions](const char* requiredDeviceExtension)
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

				auto features = device.template getFeatures2
					<
					PhysicalDeviceFeatures2,
					PhysicalDeviceVulkan11Features,
					PhysicalDeviceVulkan13Features,
					PhysicalDeviceExtendedDynamicStateFeaturesEXT
					>();
				
				bool supportsRequiredFeatures =
					features.template get<PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
					features.template get<PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
					features.template get<PhysicalDeviceVulkan13Features>().synchronization2 &&
					features.template get<PhysicalDeviceVulkan13Features>().dynamicRendering &&
					features.template get<PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

				return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
			}
		);
		if (devIter != devices.end()) m_physicalDevice = *devIter;
		else throw runtime_error("failed to find a suitable GPU!");
	}

	void CreateLogicalDevice()
	{
		vector<QueueFamilyProperties> queueFamilyProperties = m_physicalDevice.getQueueFamilyProperties();

		for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
		{
			if ((queueFamilyProperties[qfpIndex].queueFlags & QueueFlagBits::eGraphics) && m_physicalDevice.getSurfaceSupportKHR(qfpIndex, *m_surface))
			{
				m_queueIndex = qfpIndex;
				break;
			}
		}
		if (m_queueIndex == ~0) throw runtime_error("Could not find a queue for graphics and present -> terminating");

		PhysicalDeviceFeatures2 featureChain = {};
		featureChain.features.samplerAnisotropy = true;

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
		queueCreateInfo.queueFamilyIndex = m_queueIndex;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		DeviceCreateInfo createInfo = {};
		createInfo.pNext = &featureStructureChain.get<PhysicalDeviceFeatures2>();
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(m_requiredDeviceExtension.size());
		createInfo.ppEnabledExtensionNames = m_requiredDeviceExtension.data();

		m_device = raii::Device{ m_physicalDevice, createInfo };
		m_queue = raii::Queue{ m_device, m_queueIndex, 0 };
	}

	void CreateSwapChain()
	{
		SurfaceCapabilitiesKHR surfaceCapabilities = m_physicalDevice.getSurfaceCapabilitiesKHR(*m_surface);
		m_swapChainExtent = ChooseSwapExtent(surfaceCapabilities);
		m_swapChainSurfaceFormat = ChooseSwapSurfaceFormat(m_physicalDevice.getSurfaceFormatsKHR(*m_surface));

		SwapchainCreateInfoKHR swapChainCreateInfo{};
		swapChainCreateInfo.surface = *m_surface;
		swapChainCreateInfo.minImageCount = ChooseSwapMinImageCount(surfaceCapabilities);
		swapChainCreateInfo.imageFormat = m_swapChainSurfaceFormat.format;
		swapChainCreateInfo.imageColorSpace = m_swapChainSurfaceFormat.colorSpace;
		swapChainCreateInfo.imageExtent = m_swapChainExtent;
		swapChainCreateInfo.imageArrayLayers = 1;
		swapChainCreateInfo.imageUsage = ImageUsageFlagBits::eColorAttachment;
		swapChainCreateInfo.imageSharingMode = SharingMode::eExclusive;
		swapChainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
		swapChainCreateInfo.compositeAlpha = CompositeAlphaFlagBitsKHR::eOpaque;
		swapChainCreateInfo.presentMode = ChooseSwapPresentMode(m_physicalDevice.getSurfacePresentModesKHR(*m_surface));
		swapChainCreateInfo.clipped = true;

		m_swapChain = raii::SwapchainKHR{ m_device, swapChainCreateInfo };
		m_swapChainImages = m_swapChain.getImages();
	}

	void CreateImageViews()
	{
		assert(m_swapChainImageViews.empty());

		ImageViewCreateInfo imageViewCreateInfo{};
		imageViewCreateInfo.viewType = ImageViewType::e2D;
		imageViewCreateInfo.format = m_swapChainSurfaceFormat.format;
		imageViewCreateInfo.subresourceRange.aspectMask = ImageAspectFlagBits::eColor;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.levelCount = 1;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.layerCount = 1;

		for (const auto& image : m_swapChainImages)
		{
			imageViewCreateInfo.image = image;
			m_swapChainImageViews.emplace_back(m_device, imageViewCreateInfo);
		}
	}

	void CreateDescriptorSetLayout()
	{
		array bindingFlags =
		{
			DescriptorSetLayoutBinding
			(
				0,
				DescriptorType::eUniformBuffer,
				1,
				ShaderStageFlagBits::eVertex,
				nullptr
			),
			DescriptorSetLayoutBinding
			(
				1,
				DescriptorType::eCombinedImageSampler,
				1,
				ShaderStageFlagBits::eFragment,
				nullptr
			)
		};

		DescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
		layoutInfo.pBindings = bindingFlags.data();

		m_descriptorSetLayout = raii::DescriptorSetLayout{ m_device, layoutInfo };
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

		VertexInputBindingDescription bindingDescription = Vertex::getBindingDescription();
		array<VertexInputAttributeDescription, 3> attributeDescriptions = Vertex::getAttributeDescriptions();

		PipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

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
		rasterizer.frontFace = FrontFace::eCounterClockwise;
		rasterizer.depthBiasEnable = False;
		rasterizer.depthBiasSlopeFactor = 1.0f;
		rasterizer.lineWidth = 1.0f;

		PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = SampleCountFlagBits::e1;
		multisampling.sampleShadingEnable = False;

		PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = True;
		depthStencil.depthWriteEnable = True;
		depthStencil.depthCompareOp = CompareOp::eLess;
		depthStencil.depthBoundsTestEnable = False;
		depthStencil.stencilTestEnable = False;

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
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &*m_descriptorSetLayout;
		pipelineLayoutInfo.pushConstantRangeCount = 0;

		m_pipelineLayout = raii::PipelineLayout{ m_device, pipelineLayoutInfo };

		GraphicsPipelineCreateInfo graphicsPipelineCreateInfo{};
		graphicsPipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		graphicsPipelineCreateInfo.pStages = shaderStages.data();
		graphicsPipelineCreateInfo.pVertexInputState = &vertexInputInfo;
		graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssembly;
		graphicsPipelineCreateInfo.pViewportState = &viewportState;
		graphicsPipelineCreateInfo.pRasterizationState = &rasterizer;
		graphicsPipelineCreateInfo.pMultisampleState = &multisampling;
		graphicsPipelineCreateInfo.pDepthStencilState = &depthStencil;
		graphicsPipelineCreateInfo.pColorBlendState = &colorBlending;
		graphicsPipelineCreateInfo.pDynamicState = &dynamicState;
		graphicsPipelineCreateInfo.layout = m_pipelineLayout;

		PipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
		pipelineRenderingCreateInfo.colorAttachmentCount = 1;
		pipelineRenderingCreateInfo.pColorAttachmentFormats = &m_swapChainSurfaceFormat.format;
		pipelineRenderingCreateInfo.depthAttachmentFormat = FindDepthFormat();

		StructureChain<GraphicsPipelineCreateInfo, PipelineRenderingCreateInfo> pipelineCreateInfoChain
		{
			graphicsPipelineCreateInfo,
			pipelineRenderingCreateInfo
		};

		m_graphicsPipeline = raii::Pipeline{ m_device, nullptr, pipelineCreateInfoChain.get<GraphicsPipelineCreateInfo>() };
	}

	void CreateCommandPool()
	{
		CommandPoolCreateInfo poolInfo{};
		poolInfo.flags = CommandPoolCreateFlagBits::eResetCommandBuffer;
		poolInfo.queueFamilyIndex = m_queueIndex;

		m_commandPool = raii::CommandPool{ m_device, poolInfo };
	}

	void CreateDepthResources()
	{
		Format depthFormat = FindDepthFormat();
		CreateImage
		(
			m_swapChainExtent.width,
			m_swapChainExtent.height,
			depthFormat,
			ImageTiling::eOptimal,
			ImageUsageFlagBits::eDepthStencilAttachment,
			MemoryPropertyFlagBits::eDeviceLocal,
			m_depthImage,
			m_depthImageMemory
		);

		m_depthImageView = CreateImageView(m_depthImage, depthFormat, ImageAspectFlagBits::eDepth);
		TransitionImageLayout(m_depthImage, ImageLayout::eUndefined, ImageLayout::eDepthStencilAttachmentOptimal);
	}

	Format FindDepthFormat()
	{
		return FindSupportedFormat
		(
			{ Format::eD32Sfloat, Format::eD32SfloatS8Uint, Format::eD24UnormS8Uint },
			ImageTiling::eOptimal,
			FormatFeatureFlagBits::eDepthStencilAttachment
		);
	}

	Format FindSupportedFormat(const vector<Format>& candidates, ImageTiling tiling, FormatFeatureFlags features)
	{
		for (Format format : candidates)
		{
			FormatProperties props = m_physicalDevice.getFormatProperties(format);
			if (tiling == ImageTiling::eLinear && (props.linearTilingFeatures & features) == features) return format;
			else if (tiling == ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features) return format;
		}
		throw runtime_error("failed to find supported format!");
	}

	bool HasStencilComponent(Format format)
	{
		return format == Format::eD32SfloatS8Uint || format == Format::eD24UnormS8Uint;
	}

	void CreateTextureImage()
	{
		int texWidth = 0;
		int texHeight = 0;
		int texChannels = 0;
		stbi_uc* pixels = stbi_load("Texture/Texture.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		DeviceSize imageSize = static_cast<DeviceSize>(texWidth) * static_cast<DeviceSize>(texHeight) * STBI_rgb_alpha;

		if (!pixels) throw runtime_error("failed to load texture image!");

		raii::Buffer stagingBuffer({});
		raii::DeviceMemory stagingBufferMemory({});

		CreateBuffer
		(
			stagingBuffer,
			stagingBufferMemory,
			imageSize,
			BufferUsageFlagBits::eTransferSrc,
			MemoryPropertyFlagBits::eHostVisible | MemoryPropertyFlagBits::eHostCoherent
		);

		void* data = stagingBufferMemory.mapMemory(0, imageSize);
		memcpy(data, pixels, static_cast<size_t>(imageSize));
		stagingBufferMemory.unmapMemory();

		stbi_image_free(pixels);

		CreateImage
		(
			static_cast<uint32_t>(texWidth),
			static_cast<uint32_t>(texHeight),
			Format::eR8G8B8A8Srgb,
			ImageTiling::eOptimal,
			ImageUsageFlagBits::eTransferDst | ImageUsageFlagBits::eSampled,
			MemoryPropertyFlagBits::eDeviceLocal,
			m_textureImage,
			m_textureImageMemory
		);
		
		TransitionImageLayout(m_textureImage, ImageLayout::eUndefined, ImageLayout::eTransferDstOptimal);
		CopyBufferToImage(stagingBuffer, m_textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
		TransitionImageLayout(m_textureImage, ImageLayout::eTransferDstOptimal, ImageLayout::eShaderReadOnlyOptimal);
	}

	void CreateTextureImageView()
	{
		m_textureImageView = CreateImageView(m_textureImage, Format::eR8G8B8A8Srgb);
	}

	void CreateTextureSampler()
	{
		PhysicalDeviceProperties properties = m_physicalDevice.getProperties();

		SamplerCreateInfo samplerInfo{};
		samplerInfo.magFilter = Filter::eLinear;
		samplerInfo.minFilter = Filter::eLinear;
		samplerInfo.addressModeU = SamplerAddressMode::eRepeat;
		samplerInfo.addressModeV = SamplerAddressMode::eRepeat;
		samplerInfo.addressModeW = SamplerAddressMode::eRepeat;
		samplerInfo.anisotropyEnable = True;
		samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
		samplerInfo.borderColor = BorderColor::eIntOpaqueBlack;
		samplerInfo.unnormalizedCoordinates = False;
		samplerInfo.compareEnable = False;
		samplerInfo.mipmapMode = SamplerMipmapMode::eLinear;
		samplerInfo.mipLodBias = 0.0f;

		m_textureSampler = raii::Sampler{ m_device, samplerInfo };
	}

	raii::ImageView CreateImageView(raii::Image& image, Format format, ImageAspectFlags aspectFlags = ImageAspectFlagBits::eColor)
	{
		ImageViewCreateInfo viewInfo{};
		viewInfo.image = *image;
		viewInfo.viewType = ImageViewType::e2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = aspectFlags;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;

		return raii::ImageView{ m_device, viewInfo };
	}

	void CreateImage
	(
		uint32_t width,
		uint32_t height,
		Format format,
		ImageTiling tiling,
		ImageUsageFlags usage,
		MemoryPropertyFlags properties,
		raii::Image& image,
		raii::DeviceMemory& imageMemory
	)
	{
		ImageCreateInfo imageInfo{};
		imageInfo.imageType = ImageType::e2D;
		imageInfo.extent.width = width;
		imageInfo.extent.height = height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = tiling;
		imageInfo.initialLayout = ImageLayout::eUndefined;
		imageInfo.usage = usage;
		imageInfo.sharingMode = SharingMode::eExclusive;
		imageInfo.samples = SampleCountFlagBits::e1;
		image = raii::Image{ m_device, imageInfo };

		MemoryRequirements memRequirements = image.getMemoryRequirements();
		MemoryAllocateInfo allocInfo{};

		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

		imageMemory = raii::DeviceMemory{ m_device, allocInfo };
		image.bindMemory(*imageMemory, 0);
	}

	void TransitionImageLayout(raii::Image& image, ImageLayout oldLayout, ImageLayout newLayout)
	{
		unique_ptr<raii::CommandBuffer> commandBuffer = BeginSingleTimeCommands();

		ImageMemoryBarrier barrier{};
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.image = *image;
		barrier.subresourceRange.aspectMask = ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		PipelineStageFlags sourceStage;
		PipelineStageFlags destinationStage;

		if (newLayout == ImageLayout::eDepthStencilAttachmentOptimal)
		{
			barrier.subresourceRange.aspectMask = ImageAspectFlagBits::eDepth;
			if (HasStencilComponent(FindDepthFormat())) barrier.subresourceRange.aspectMask |= ImageAspectFlagBits::eStencil;
		}
		else barrier.subresourceRange.aspectMask = ImageAspectFlagBits::eColor;

		if (oldLayout == ImageLayout::eUndefined && newLayout == ImageLayout::eTransferDstOptimal)
		{
			barrier.srcAccessMask = {};
			barrier.dstAccessMask = AccessFlagBits::eTransferWrite;
			sourceStage = PipelineStageFlagBits::eTopOfPipe;
			destinationStage = PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == ImageLayout::eTransferDstOptimal && newLayout == ImageLayout::eShaderReadOnlyOptimal)
		{
			barrier.srcAccessMask = AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = AccessFlagBits::eShaderRead;
			sourceStage = PipelineStageFlagBits::eTransfer;
			destinationStage = PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == ImageLayout::eUndefined && newLayout == ImageLayout::eDepthStencilAttachmentOptimal)
		{
			barrier.srcAccessMask = {};
			barrier.dstAccessMask = AccessFlagBits::eDepthStencilAttachmentRead | AccessFlagBits::eDepthStencilAttachmentWrite;
			sourceStage = PipelineStageFlagBits::eTopOfPipe;
			destinationStage = PipelineStageFlagBits::eEarlyFragmentTests;
		}
		else throw invalid_argument("unsupported layout transition!");

		commandBuffer->pipelineBarrier
		(
			sourceStage,
			destinationStage,
			{},
			nullptr,
			nullptr,
			barrier
		);
		EndSingleTimeCommands(*commandBuffer);
	}

	void CopyBufferToImage
	(
		raii::Buffer& buffer,
		raii::Image& image,
		uint32_t width,
		uint32_t height
	)
	{
		unique_ptr<raii::CommandBuffer> commandBuffer = BeginSingleTimeCommands();

		BufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = ImageAspectFlagBits::eColor;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = Offset3D{ 0, 0, 0 };
		region.imageExtent = Extent3D{ width, height, 1 };

		commandBuffer->copyBufferToImage
		(
			*buffer,
			*image,
			ImageLayout::eTransferDstOptimal,
			region
		);
		EndSingleTimeCommands(*commandBuffer);
	}

	void CreateVertexBuffer()
	{
		DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
		raii::Buffer stagingBuffer({});
		raii::DeviceMemory stagingBufferMemory({});
		CreateBuffer(stagingBuffer, stagingBufferMemory, bufferSize, BufferUsageFlagBits::eTransferSrc, MemoryPropertyFlagBits::eHostVisible | MemoryPropertyFlagBits::eHostCoherent);

		void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
		memcpy(dataStaging, vertices.data(), static_cast<size_t>(bufferSize));
		stagingBufferMemory.unmapMemory();

		CreateBuffer(m_vertexBuffer, m_vertexBufferMemory, bufferSize, BufferUsageFlagBits::eTransferDst | BufferUsageFlagBits::eVertexBuffer, MemoryPropertyFlagBits::eDeviceLocal);
		CopyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);
	}

	void CreateIndexBuffer()
	{
		DeviceSize bufferSize = sizeof(indices[0]) * indices.size();
		raii::Buffer stagingBuffer({});
		raii::DeviceMemory stagingBufferMemory({});
		CreateBuffer(stagingBuffer, stagingBufferMemory, bufferSize, BufferUsageFlagBits::eTransferSrc, MemoryPropertyFlagBits::eHostVisible | MemoryPropertyFlagBits::eHostCoherent);

		void* data = stagingBufferMemory.mapMemory(0, bufferSize);
		memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
		stagingBufferMemory.unmapMemory();

		CreateBuffer(m_indexBuffer, m_indexBufferMemory, bufferSize, BufferUsageFlagBits::eTransferDst | BufferUsageFlagBits::eIndexBuffer, MemoryPropertyFlagBits::eDeviceLocal);
		CopyBuffer(stagingBuffer, m_indexBuffer, bufferSize);
	}

	void CreateUniformBuffers()
	{
		m_uniformBuffers.clear();
		m_uniformBuffersMemory.clear();
		m_uniformBuffersMapped.clear();

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			constexpr DeviceSize BUFFER_SIZE = sizeof(MatrixUB);

			raii::Buffer uniformBuffer({});
			raii::DeviceMemory uniformBufferMemory({});

			CreateBuffer(uniformBuffer, uniformBufferMemory, BUFFER_SIZE, BufferUsageFlagBits::eUniformBuffer, MemoryPropertyFlagBits::eHostVisible | MemoryPropertyFlagBits::eHostCoherent);
			m_uniformBuffers.push_back(move(uniformBuffer));
			m_uniformBuffersMemory.push_back(move(uniformBufferMemory));
			m_uniformBuffersMapped.push_back(m_uniformBuffersMemory[i].mapMemory(0, BUFFER_SIZE));
		}
	}

	void CreateDescriptorPool()
	{
		array<DescriptorPoolSize, 2> poolSizes{};
		poolSizes[0].type = DescriptorType::eUniformBuffer;
		poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
		poolSizes[1].type = DescriptorType::eCombinedImageSampler;
		poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

		DescriptorPoolCreateInfo poolInfo{};
		poolInfo.flags = DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();

		m_descriptorPool = raii::DescriptorPool{ m_device, poolInfo };
	}

	void CreateDescriptorSets()
	{
		vector<DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *m_descriptorSetLayout);
		DescriptorSetAllocateInfo allocInfo{};
		allocInfo.descriptorPool = *m_descriptorPool;
		allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
		allocInfo.pSetLayouts = layouts.data();

		m_descriptorSets.clear();
		m_descriptorSets = m_device.allocateDescriptorSets(allocInfo);

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			constexpr size_t BUFFER_SIZE = sizeof(MatrixUB);

			DescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = *m_uniformBuffers[i];
			bufferInfo.offset = 0;
			bufferInfo.range = BUFFER_SIZE;

			DescriptorImageInfo imageInfo{};
			imageInfo.imageLayout = ImageLayout::eShaderReadOnlyOptimal;
			imageInfo.imageView = *m_textureImageView;
			imageInfo.sampler = *m_textureSampler;

			array<WriteDescriptorSet, 2> descriptorWrites{};
			descriptorWrites[0].dstSet = *m_descriptorSets[i];
			descriptorWrites[0].dstBinding = 0;
			descriptorWrites[0].dstArrayElement = 0;
			descriptorWrites[0].descriptorType = DescriptorType::eUniformBuffer;
			descriptorWrites[0].descriptorCount = 1;
			descriptorWrites[0].pBufferInfo = &bufferInfo;
			descriptorWrites[1].dstSet = *m_descriptorSets[i];
			descriptorWrites[1].dstBinding = 1;
			descriptorWrites[1].dstArrayElement = 0;
			descriptorWrites[1].descriptorType = DescriptorType::eCombinedImageSampler;
			descriptorWrites[1].descriptorCount = 1;
			descriptorWrites[1].pImageInfo = &imageInfo;

			m_device.updateDescriptorSets(descriptorWrites, {});
		}
	}

	void CreateBuffer
	(
		raii::Buffer& buffer,
		raii::DeviceMemory& bufferMemory,
		DeviceSize size,
		BufferUsageFlags usage,
		MemoryPropertyFlags properties
	)
	{
		BufferCreateInfo bufferInfo{};
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = SharingMode::eExclusive;
		buffer = raii::Buffer{ m_device, bufferInfo };

		MemoryRequirements memRequirements = buffer.getMemoryRequirements();
		MemoryAllocateInfo memoryAllocateInfo{};
		memoryAllocateInfo.allocationSize = memRequirements.size;
		memoryAllocateInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);
		bufferMemory = raii::DeviceMemory{ m_device, memoryAllocateInfo };

		buffer.bindMemory(*bufferMemory, 0);
	}

	unique_ptr<raii::CommandBuffer> BeginSingleTimeCommands()
	{
		CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = m_commandPool;
		allocInfo.level = CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		raii::CommandBuffer commandBuffer = move(m_device.allocateCommandBuffers(allocInfo).front());
		commandBuffer.begin(CommandBufferBeginInfo{ CommandBufferUsageFlagBits::eOneTimeSubmit });

		return make_unique<raii::CommandBuffer>(move(commandBuffer));
	}

	void EndSingleTimeCommands(raii::CommandBuffer& commandBuffer)
	{
		commandBuffer.end();

		SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*commandBuffer;

		m_queue.submit(submitInfo, nullptr);
		m_queue.waitIdle();
	}

	void CopyBuffer(raii::Buffer& srcBuffer, raii::Buffer& dstBuffer, DeviceSize size)
	{
		CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = m_commandPool;
		allocInfo.level = CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;

		raii::CommandBuffer commandCopyBuffer = move(m_device.allocateCommandBuffers(allocInfo).front());
		commandCopyBuffer.begin(CommandBufferBeginInfo{ CommandBufferUsageFlagBits::eOneTimeSubmit });

		BufferCopy copyRegion{};
		copyRegion.size = size;
		commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, copyRegion);
		commandCopyBuffer.end();

		SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*commandCopyBuffer;
		m_queue.submit(submitInfo, nullptr);
		m_queue.waitIdle();
	}

	uint32_t FindMemoryType(uint32_t typeFilter, MemoryPropertyFlags properties)
	{
		PhysicalDeviceMemoryProperties memProperties = m_physicalDevice.getMemoryProperties();
		for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
		{
			if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) return i;
		}

		throw runtime_error("failed to find suitable memory type!");
	}

	void CreateCommandBuffer()
	{
		CommandBufferAllocateInfo allocInfo{};
		allocInfo.commandPool = m_commandPool;
		allocInfo.level = CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

		m_commandBuffers = raii::CommandBuffers{ m_device, allocInfo };
	}

	void RecordCommandBuffer(uint32_t imageIndex)
	{
		m_commandBuffers[m_currentFrame].begin(CommandBufferBeginInfo{});

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
		colorAttachmentInfo.imageView = *m_swapChainImageViews[imageIndex];
		colorAttachmentInfo.imageLayout = ImageLayout::eColorAttachmentOptimal;
		colorAttachmentInfo.loadOp = AttachmentLoadOp::eClear;
		colorAttachmentInfo.storeOp = AttachmentStoreOp::eStore;
		colorAttachmentInfo.clearValue = clearColor;

		RenderingInfo renderingInfo{};
		renderingInfo.renderArea.offset = Offset2D{ 0, 0 };
		renderingInfo.renderArea.extent = m_swapChainExtent;
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments = &colorAttachmentInfo;

		m_commandBuffers[m_currentFrame].beginRendering(renderingInfo);

		m_commandBuffers[m_currentFrame].bindPipeline(PipelineBindPoint::eGraphics, *m_graphicsPipeline);

		m_commandBuffers[m_currentFrame].setViewport(0, Viewport{ 0.0f, 0.0f, static_cast<float>(m_swapChainExtent.width), static_cast<float>(m_swapChainExtent.height), 0.0f, 1.0f });
		m_commandBuffers[m_currentFrame].setScissor(0, Rect2D{ Offset2D{ 0, 0 }, m_swapChainExtent });

		m_commandBuffers[m_currentFrame].bindDescriptorSets(PipelineBindPoint::eGraphics, *m_pipelineLayout, 0, { *m_descriptorSets[m_currentFrame] }, {});
		m_commandBuffers[m_currentFrame].bindVertexBuffers(0, { *m_vertexBuffer }, { 0 });
		m_commandBuffers[m_currentFrame].bindIndexBuffer(*m_indexBuffer, 0, IndexTypeValue<decltype(indices)::value_type>::value);
		m_commandBuffers[m_currentFrame].drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

		m_commandBuffers[m_currentFrame].endRendering();

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

		m_commandBuffers[m_currentFrame].end();
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
		barrier.image = m_swapChainImages[imageIndex];
		barrier.subresourceRange.aspectMask = ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		DependencyInfo dependencyInfo{};
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &barrier;
		m_commandBuffers[m_currentFrame].pipelineBarrier2(dependencyInfo);
	}

	void CreateSyncObjects()
	{
		m_presentCompleteSemaphore.clear();
		m_renderFinishedSemaphore.clear();
		m_inFlightFences.clear();

		for (size_t i = 0; i < m_swapChainImages.size(); i++)
		{
			m_presentCompleteSemaphore.emplace_back(m_device, SemaphoreCreateInfo{});
			m_renderFinishedSemaphore.emplace_back(m_device, SemaphoreCreateInfo{});
		}

		FenceCreateInfo fenceInfo{};
		fenceInfo.flags = FenceCreateFlagBits::eSignaled;

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) m_inFlightFences.emplace_back(m_device, fenceInfo);
	}

	void UpdateUniformBuffer(uint32_t currentFrame)
	{
		static chrono::steady_clock::time_point startTime = chrono::high_resolution_clock::now();

		const chrono::steady_clock::time_point currentTime = chrono::high_resolution_clock::now();
		const float time = chrono::duration<float>(currentTime - startTime).count();

		MatrixUB MUB{};
		MUB.world = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		MUB.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		MUB.proj = glm::perspective(glm::radians(45.0f), static_cast<float>(m_swapChainExtent.width) / static_cast<float>(m_swapChainExtent.height), 0.1f, 10.0f);
		MUB.proj[1][1] *= -1.0f;
		MUB.WVP = MUB.proj * MUB.view * MUB.world;

		constexpr size_t BUFFER_SIZE = sizeof(MUB);

		memcpy(m_uniformBuffersMapped[currentFrame], &MUB, BUFFER_SIZE);
	}

	void DrawFrame()
	{
		while (m_device.waitForFences(*m_inFlightFences[m_currentFrame], True, UINT64_MAX) == Result::eTimeout);

		auto [result, imageIndex] = m_swapChain.acquireNextImage(UINT64_MAX, *m_presentCompleteSemaphore[m_semaphoreIndex], nullptr);

		if (result == Result::eErrorOutOfDateKHR) { RecreateSwapChain(); return; }
		else if (result != Result::eSuccess && result != Result::eSuboptimalKHR) throw runtime_error("failed to acquire swap chain image!");

		UpdateUniformBuffer(m_currentFrame);

		m_device.resetFences(*m_inFlightFences[m_currentFrame]);
		m_commandBuffers[m_currentFrame].reset();
		RecordCommandBuffer(imageIndex);

		PipelineStageFlags waitDestinationStageMask = PipelineStageFlagBits::eColorAttachmentOutput;

		SubmitInfo submitInfo{};
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &*m_presentCompleteSemaphore[m_semaphoreIndex];
		submitInfo.pWaitDstStageMask = &waitDestinationStageMask;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*m_commandBuffers[m_currentFrame];
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &*m_renderFinishedSemaphore[imageIndex];
		m_queue.submit(submitInfo, *m_inFlightFences[m_currentFrame]);

		PresentInfoKHR presentInfoKHR{};
		presentInfoKHR.waitSemaphoreCount = 1;
		presentInfoKHR.pWaitSemaphores = &*m_renderFinishedSemaphore[imageIndex];
		presentInfoKHR.swapchainCount = 1;
		presentInfoKHR.pSwapchains = &*m_swapChain;
		presentInfoKHR.pImageIndices = &imageIndex;
		
		try
		{
			result = m_queue.presentKHR(presentInfoKHR);

			if (result == Result::eErrorOutOfDateKHR || result == Result::eSuboptimalKHR || m_framebufferResized)
			{
				m_framebufferResized = false;
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

		m_semaphoreIndex = (m_semaphoreIndex + 1) % m_presentCompleteSemaphore.size();
		m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}

	[[nodiscard]] raii::ShaderModule CreateShaderModule(const vector<char>& code) const
	{
		ShaderModuleCreateInfo createInfo{};
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		raii::ShaderModule shaderModule{ m_device, createInfo };

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
		glfwGetFramebufferSize(m_window.get(), &width, &height);

		return
		{
			clamp<uint32_t>(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			clamp<uint32_t>(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
		};
	}

	vector<const char*> GetRequiredExtensions() const
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