// ============================================================================
// Unsuffered - The Fallen
// main.cpp - Application entry point
// ============================================================================
// "In a world where the gods are dead and the Fallen reign,
//  one hobgoblin discovers that the will to bond is the will to power."
// ============================================================================

#include "Engine.h"

int main(int argc, char* argv[]) {
    auto& engine = Unsuffered::Engine::Instance();

    if (!engine.Initialize("Unsuffered - The Fallen", 1920, 1080)) {
        return -1;
    }

    engine.Run();
    engine.Shutdown();

    return 0;
}
