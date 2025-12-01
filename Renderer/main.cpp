#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#else
import vulkan_hpp;
#endif

#include <vulkan/vulkan_raii.hpp>

#include <GLFW/glfw3.h>

#include <memory>
#include <iostream>
#include <stdexcept>
#include <cstdlib>

using namespace std;
using namespace vk;

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

class HelloTriangleApplication
{
	unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window{ nullptr, glfwDestroyWindow };

	raii::Context context{};
	raii::Instance instance = nullptr;

	void InitWindow()
	{
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window.reset(glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr));
	}

	void InitVulkan()
	{

	}

	void MainLoop()
	{
		while (!glfwWindowShouldClose(window.get())) glfwPollEvents();
	}

	void Cleanup()
	{
		glfwTerminate();
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