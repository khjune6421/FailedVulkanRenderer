#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#else
import vulkan_hpp;
#endif

#include <vulkan/vulkan_raii.hpp>

#include <GLFW/glfw3.h>

#include <memory>
#include <iostream>
#include <cstdlib>

using namespace std;
using namespace vk;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

const vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
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
	raii::Queue graphicsQueue = nullptr;
	raii::SwapchainKHR swapChain = nullptr;

	vector<Image> swapChainImages;
	SurfaceFormatKHR swapChainSurfaceFormat{};
	Extent2D swapChainExtent{};
	vector<raii::ImageView> swapChainImageViews;

	vector<const char*> requiredDeviceExtension =
	{
		KHRSwapchainExtensionName,
		KHRSpirv14ExtensionName,
		KHRSynchronization2ExtensionName,
		KHRCreateRenderpass2ExtensionName
	};

	void InitWindow()
	{
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window.reset(glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr));
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
	}

	void MainLoop()
	{
		while (!glfwWindowShouldClose(window.get())) glfwPollEvents();
	}

	void Cleanup()
	{
		glfwTerminate();
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
		if (enableValidationLayers) requiredLayers.assign(validationLayers.begin(), validationLayers.end());

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

		InstanceCreateInfo createInfo
		{
			{},
			&appInfo,
			static_cast<uint32_t>(requiredLayers.size()),
			requiredLayers.data(),
			static_cast<uint32_t>(requiredExtensions.size()),
			requiredExtensions.data()
		};

		instance = raii::Instance{ context, createInfo };
	}

	void SetupDebugMessenger()
	{
		if (!enableValidationLayers) return;

		DebugUtilsMessageSeverityFlagsEXT severityFlags(DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | DebugUtilsMessageSeverityFlagBitsEXT::eWarning | DebugUtilsMessageSeverityFlagBitsEXT::eError);
		DebugUtilsMessageTypeFlagsEXT messageTypeFlags(DebugUtilsMessageTypeFlagBitsEXT::eGeneral | DebugUtilsMessageTypeFlagBitsEXT::ePerformance | DebugUtilsMessageTypeFlagBitsEXT::eValidation);
		DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT
		{
			{},
			severityFlags,
			messageTypeFlags,
			&DebugCallback
		};
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

				auto features = device.template getFeatures2<PhysicalDeviceFeatures2, PhysicalDeviceVulkan13Features, PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
				bool supportsRequiredFeatures = features.template get<PhysicalDeviceVulkan13Features>().dynamicRendering &&
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
		auto graphicsQueueFamilyProperty = ranges::find_if(queueFamilyProperties, [](const QueueFamilyProperties& qfp) { return (qfp.queueFlags & QueueFlagBits::eGraphics) != static_cast<QueueFlags>(0); });
		assert(graphicsQueueFamilyProperty != queueFamilyProperties.end() && "No graphics queue family found!");

		uint32_t graphicsIndex = static_cast<uint32_t>(distance(queueFamilyProperties.begin(), graphicsQueueFamilyProperty));

		PhysicalDeviceFeatures2 featureChain = {};
		PhysicalDeviceVulkan13Features vulkan13Features = {};
		vulkan13Features.dynamicRendering = true;
		PhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicStateFeatures = {};
		extendedDynamicStateFeatures.extendedDynamicState = true;

		StructureChain<PhysicalDeviceFeatures2, PhysicalDeviceVulkan13Features, PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureStructureChain
		{
			featureChain,
			vulkan13Features,
			extendedDynamicStateFeatures
		};

		constexpr float queuePriority = 0.5f;
		DeviceQueueCreateInfo queueCreateInfo
		{
			{},
			graphicsIndex,
			1,
			&queuePriority
		};

		DeviceCreateInfo createInfo = {};
		createInfo.pNext = &featureStructureChain.get<PhysicalDeviceFeatures2>();
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size());
		createInfo.ppEnabledExtensionNames = requiredDeviceExtension.data();

		device = raii::Device{ physicalDevice, createInfo };
		graphicsQueue = raii::Queue{ device, graphicsIndex, 0 };
	}

	void CreateSwapChain()
	{
		SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
		swapChainExtent = ChooseSwapExtent(surfaceCapabilities);
		swapChainSurfaceFormat = ChooseSwapSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(*surface));

		SwapchainCreateInfoKHR swapChainCreateInfo
		{
			{},
			*surface,
			ChooseSwapMinImageCount(surfaceCapabilities),
			swapChainSurfaceFormat.format,
			swapChainSurfaceFormat.colorSpace,
			swapChainExtent,
			1,
			ImageUsageFlagBits::eColorAttachment,
			SharingMode::eExclusive,
			0,
			nullptr,
			surfaceCapabilities.currentTransform,
			CompositeAlphaFlagBitsKHR::eOpaque,
			ChooseSwapPresentMode(physicalDevice.getSurfacePresentModesKHR(*surface)),
			true
		};

		swapChain = raii::SwapchainKHR{ device, swapChainCreateInfo };
		swapChainImages = swapChain.getImages();
	}

	void CreateImageViews()
	{
		assert(swapChainImageViews.empty());
		ImageViewCreateInfo imageViewCreateInfo
		{
			{},
			{},
			ImageViewType::e2D,
			swapChainSurfaceFormat.format,
			ComponentMapping{},
			ImageSubresourceRange{ ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
		};

		for (const auto& image : swapChainImages)
		{
			imageViewCreateInfo.image = image;
			swapChainImageViews.emplace_back(device, imageViewCreateInfo);
		}
	}

	void CreateGraphicsPipeline()
	{

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

		if (enableValidationLayers) extensions.push_back(EXTDebugUtilsExtensionName);

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