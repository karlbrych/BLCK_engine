#pragma once

#include <iostream>
#include <stdexcept>
#include <string>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <Renderer.h>
#include <ScriptManager.h>

// Owns the window, the GL context and the two subsystems the gameplay scripts
// talk to. The frame loop itself is deliberately thin: it advances time, lets
// every script update and submit its draw calls, and flushes the queue.
class Application
{
public:
    void run(int width, int height, const std::string& title);

private:
    void createWindow();
    void loop();
    void destroyWindow();

    GLFWwindow* window = nullptr;
    std::string title;
    int width = 0;
    int height = 0;

    Renderer renderer;
    ScriptManager scripts{"gameplay"};

    // Scripts are only stat()ed a few times a second; doing it per frame would
    // put a directory walk in the hot loop for no benefit.
    double hotReloadInterval = 0.5;
};
