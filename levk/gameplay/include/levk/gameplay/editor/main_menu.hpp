#pragma once
#include <levk/gameplay/editor/types.hpp>

namespace le::editor {
class MainMenu {
  public:
	MainMenu();

	void operator()(MenuList const& extras) const;

  private:
	MenuList m_main;
};
} // namespace le::editor
