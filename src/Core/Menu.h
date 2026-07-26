#pragma once
#include <string>
#include <utility>
#include <vector>

class Menu {
public:
  static void Draw();
  static void Update();
  static void Toggle();
  static bool IsOpen();
  static void Initialize();

private:
  static void DrawRect(float x, float y, float width, float height, int r,
                       int g, int b, int a);
  static void DrawTextStr(const std::string &text, float x, float y,
                          float scale, int r, int g, int b, int a,
                          bool center = false);

  struct MenuItem {
    std::string name;
    enum Type { Bool, Float, IntChoice, Submenu, Action, KeyBind } type;

    bool *boolVal = nullptr;

    float *floatVal = nullptr;
    float floatStep = 0.01f;
    float floatMin = 0.0f;
    float floatMax = 1.0f;

    int *keyVal = nullptr;
    int intMin = 0;
    int intMax = 0;
    std::vector<std::string> choiceLabels;

    int targetSubmenu = -1;
    int actionId = 0;

    // Constructor for Submenu
    MenuItem(std::string n, Type t, int target)
        : name(n), type(t) {
      if (t == Action)
        actionId = target;
      else
        targetSubmenu = target;
    }

    // Constructor for Bool
    MenuItem(std::string n, Type t, bool *b) : name(n), type(t), boolVal(b) {}

    // Constructor for Float
    MenuItem(std::string n, Type t, float *f, float step, float minV,
             float maxV)
        : name(n), type(t), floatVal(f), floatStep(step), floatMin(minV),
          floatMax(maxV) {}

    // Constructor for KeyBind. Pass the address of one of the Config::KeyXxx
    // ints (e.g. &Config::KeyShiftUp) and it becomes rebindable in-menu.
    MenuItem(std::string n, Type t, int *k) : name(n), type(t), keyVal(k) {}

    MenuItem(std::string n, Type t, int *value, int minV, int maxV,
             std::vector<std::string> labels)
        : name(n), type(t), keyVal(value), intMin(minV), intMax(maxV),
          choiceLabels(std::move(labels)) {}
  };

  struct Submenu {
    std::string title;
    std::vector<MenuItem> items;
    int selectedIndex = 0;
  };

  static bool isOpen;
  static std::vector<Submenu> menus;
  static std::vector<int> menuStack;

  // True while the menu is waiting for the next keypress to bind to the
  // selected KeyBind item (Enter was pressed on it). Any key (including
  // mouse-adjacent VKs) confirms the bind; Escape cancels without changing
  // it.
  static bool waitingForKeyBind;

  static std::string VkName(int vk);

  static int GetCurrentMenuIndex();
  static Submenu &GetCurrentMenu();
};
