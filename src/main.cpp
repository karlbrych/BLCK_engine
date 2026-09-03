#include <Application.h>


int main(){
    try {
        Application app;
        app.run(1920, 1080, "BLCK Engine");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
}