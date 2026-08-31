#include <Application.h>


int main(){
    try {
        Application app;
        app.run(800, 600, "Zero to Hero");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
}