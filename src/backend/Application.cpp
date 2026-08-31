#include <Application.h>

void Application::createWindow()
{
    if (!glfwInit())
    {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

    this->window =
        glfwCreateWindow(this->width, this->height, this->title.c_str(), nullptr, nullptr);
    if (!this->window)
    {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwMakeContextCurrent(this->window);
    glfwSwapInterval(1);

    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress))
    {
        glfwDestroyWindow(this->window);
        glfwTerminate();
        throw std::runtime_error("Failed to initialize glad2");
    }
}

void Application::destroyWindow()
{
    // Order matters: both hold GL objects that must die while the context is
    // still current, and the scripts own meshes the renderer may still list.
    scripts.shutdown();
    renderer.shutdown();

    if (this->window)
    {
        glfwDestroyWindow(this->window);
        this->window = nullptr;
    }
    glfwTerminate();
}

void Application::loop()
{
    double previous = glfwGetTime();
    double nextHotReloadCheck = previous + hotReloadInterval;

    while (!glfwWindowShouldClose(this->window))
    {
        const double now = glfwGetTime();
        const double delta = now - previous;
        previous = now;

        if (now >= nextHotReloadCheck)
        {
            scripts.reloadChanged();
            nextHotReloadCheck = now + hotReloadInterval;
        }

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(this->window, &framebufferWidth, &framebufferHeight);
        renderer.setViewport(framebufferWidth, framebufferHeight);

        renderer.beginFrame();
        scripts.update(delta);
        scripts.draw(); // this is where the scripts' draw calls land in the queue
        renderer.flush();

        glfwSwapBuffers(this->window);
        glfwPollEvents();
    }
}

void Application::run(int width, int height, const std::string& title)
{
    this->width = width;
    this->height = height;
    this->title = title;

    createWindow();

    try
    {
        renderer.init();
        renderer.setViewport(width, height);

        scripts.bind(renderer, this->window);
        const std::size_t loaded = scripts.loadAll();
        std::cout << "ScriptManager: loaded " << loaded << " script(s) from "
                  << scripts.root().string() << "/\n";

        scripts.start();
        loop();
    }
    catch (...)
    {
        destroyWindow();
        throw;
    }

    destroyWindow();
}
