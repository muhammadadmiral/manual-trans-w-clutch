#pragma once
#include <string>
#include <vector>

class Menu {
public:
    static void Draw();
    static void Update();
    static void Toggle();
    static bool IsOpen();
    static void Initialize();

private:
    static void DrawRect(float x, float y, float width, float height, int r, int g, int b, int a);
    static void DrawTextStr(const std::string& text, float x, float y, float scale, int r, int g, int b, int a, bool center = false);
    
    struct MenuItem {
        std::string name;
        enum Type { Bool, Float, Submenu, Action } type;
        
        bool* boolVal = nullptr;
        
        float* floatVal = nullptr;
        float floatStep = 0.01f;
        float floatMin = 0.0f;
        float floatMax = 1.0f;
        
        int targetSubmenu = -1;

        // Constructor for Submenu
        MenuItem(std::string n, Type t, int target) 
            : name(n), type(t), targetSubmenu(target) {}
            
        // Constructor for Bool
        MenuItem(std::string n, Type t, bool* b) 
            : name(n), type(t), boolVal(b) {}
            
        // Constructor for Float
        MenuItem(std::string n, Type t, float* f, float step, float minV, float maxV) 
            : name(n), type(t), floatVal(f), floatStep(step), floatMin(minV), floatMax(maxV) {}
    };

    struct Submenu {
        std::string title;
        std::vector<MenuItem> items;
        int selectedIndex = 0;
    };

    static bool isOpen;
    static std::vector<Submenu> menus;
    static std::vector<int> menuStack;
    
    static int GetCurrentMenuIndex();
    static Submenu& GetCurrentMenu();
};
