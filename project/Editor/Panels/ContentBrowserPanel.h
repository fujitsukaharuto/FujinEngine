#pragma once
#include <string>

namespace Fujin {

class ContentBrowserPanel {
public:
    void Draw();
private:
    std::string m_currentPath = "Resource";
    char        m_searchBuf[128] = {};
};

} // namespace Fujin
