#pragma once

#include <iostream>
#include <stdexcept>
#include <string>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <Renderer.h>
#include <ScriptManager.h>

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
    double hotReloadInterval = 0.5;
};
