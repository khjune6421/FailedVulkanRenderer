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

const vector<char const*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
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
	raii::PhysicalDevice physicalDevice = nullptr;

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

		vector<const char*> requiredExtensions = getRequiredExtensions();
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
			&debugCallback
		};
		debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
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
				bool supportsAllRequiredExtensions = ranges::all_of(requiredDeviceExtension, [&availableDeviceExtensions](const char* requiredDeviceExtension)
					{
					return ranges::any_of(availableDeviceExtensions, [requiredDeviceExtension](const ExtensionProperties& availableDeviceExtension)
						{
							return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0;
						});
					});

				auto features = device.template getFeatures2<PhysicalDeviceFeatures2, PhysicalDeviceVulkan13Features, PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
				bool supportsRequiredFeatures = features.template get<PhysicalDeviceVulkan13Features>().dynamicRendering &&
					features.template get<PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

				return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
			});
	}

	vector<const char*> getRequiredExtensions()
	{
		uint32_t glfwExtensionCount = 0;

		const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
		vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

		if (enableValidationLayers) extensions.push_back(EXTDebugUtilsExtensionName);

		return extensions;
	}

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback
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