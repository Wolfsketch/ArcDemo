#include <windows.h>
#include "Game.h"

int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE,
    LPSTR,
    int showCommand)
{
    Game game;

    if (!game.Initialize(instance, showCommand))
        return 1;

    return game.Run();
}
