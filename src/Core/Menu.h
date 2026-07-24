#pragma once
#include <string>
#include <vector>

class Menu {
public:
    static void Draw();
    static void Update();
    static void Toggle();
    static bool IsOpen();

private:
    static void DrawRect(float x, float y, float width, float height, int r, int g, int b, int a);
    static void DrawTextStr(const std::string& text, float x, float y, float scale, int r, int g, int b, int a, bool center = false);
    
    static bool isOpen;
    static int selectedIndex;
    static const int numItems = 4;
};
