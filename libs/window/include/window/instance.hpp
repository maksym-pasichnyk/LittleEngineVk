#pragma once
#include <cassert>
#include <core/lib_logger.hpp>
#include <core/mono_instance.hpp>
#include <window/event_queue.hpp>
#include <window/surface.hpp>

namespace le::window {
class IInstance : public TMonoInstance<IInstance>, public ISurface {
  public:
	struct CreateInfo;

	IInstance(bool bValid) : TMonoInstance(bValid) {}
	IInstance(IInstance&&) = default;
	IInstance& operator=(IInstance&&) = default;
	virtual ~IInstance() = default;

	bool isDesktop() const noexcept { return m_desktop; }

	virtual EventQueue pollEvents() = 0;

	virtual void show() const {}
	virtual glm::ivec2 framebufferSize() const noexcept { return {0, 0}; }

  protected:
	LibLogger m_log;
	bool m_desktop = false;
};

enum class Style { eDecoratedWindow = 0, eBorderlessWindow, eBorderlessFullscreen, eDedicatedFullscreen, eCOUNT_ };
constexpr static EnumArray<Style> const styleNames = {"Decorated Window", "Borderless Window", "Borderless Fullscreen", "Dedicated Fullscreen"};

struct IInstance::CreateInfo {
	struct {
		// Desktop
		std::string title;
		glm::ivec2 size = {32, 32};
		glm::ivec2 centreOffset = {};
		// Android
		ErasedRef androidApp;
	} config;

	struct {
		// Common
		LibLogger::Verbosity verbosity = LibLogger::Verbosity::eLibUser;
		// Desktop
		Style style = Style::eDecoratedWindow;
		u8 screenID = 0;
		bool bCentreCursor = true;
		bool bAutoShow = false;
	} options;
};

Key parseKey(std::string_view str) noexcept;
Action parseAction(std::string_view str) noexcept;
Mods parseMods(View<std::string> vec) noexcept;
Axis parseAxis(std::string_view str) noexcept;
} // namespace le::window
