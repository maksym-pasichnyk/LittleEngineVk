#pragma once
#if defined(LEVK_DESKTOP)
#include <window/instance.hpp>

namespace le::window {
class DesktopInstance final : public IInstance {
  public:
	DesktopInstance() : IInstance(false) {
	}
	explicit DesktopInstance(CreateInfo const& info);
	DesktopInstance(DesktopInstance&&) = default;
	DesktopInstance& operator=(DesktopInstance&&) = default;
	~DesktopInstance();

	// ISurface
	View<std::string_view> vkInstanceExtensions() const override;
	bool vkCreateSurface(ErasedRef vkInstance, ErasedRef out_vkSurface) const override;
	ErasedRef nativePtr() const noexcept override;

	// IInstance
	EventQueue pollEvents() override;
	void show() const override;
	glm::ivec2 framebufferSize() const noexcept override;

	// Desktop
	glm::ivec2 windowSize() const noexcept;
	CursorType cursorType() const noexcept;
	CursorMode cursorMode() const noexcept;
	glm::vec2 cursorPosition() const noexcept;

	void cursorType(CursorType type);
	void cursorMode(CursorMode mode);
	void cursorPosition(glm::vec2 position);

	void close();

	bool importControllerDB(std::string_view db) const;
	kt::fixed_vector<Gamepad, 8> activeGamepads() const;
	Joystick joyState(s32 id) const;
	Gamepad gamepadState(s32 id) const;
	f32 triggerToAxis(f32 triggerValue) const;
	std::size_t joystickAxesCount(s32 id) const;
	std::size_t joysticKButtonsCount(s32 id) const;
	std::string_view toString(s32 key) const;
};
} // namespace le::window
#endif
