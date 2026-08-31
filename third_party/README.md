# Third-party dependencies

This project expects the following base dependencies:
- Window backend: GLFW
- OpenGL loader: glad2

Recommended ways to install:
- vcpkg (Windows): vcpkg install glfw3 sdl3 glad glew
- conan: declare packages in conanfile
- system package manager on Linux/macOS

Optional libraries selected by generator:
- GLM: yes
- Dear ImGui: yes
- stb_image: yes
- Assimp: yes
- fmt: yes
- spdlog: yes
