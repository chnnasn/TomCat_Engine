#pragma once
#include "TomCat/Core/Window.h"
struct GLFWwindow;
namespace TomCat {
class WebWindow final : public Window {
public:
  explicit WebWindow(const WindowProps& props);
  ~WebWindow() override;
  void PollEvents() override;
  void Present() override;
  uint32_t GetWidth() const override { return m_Width; }
  uint32_t GetHeight() const override { return m_Height; }
  void SetTitle(const std::string& title) override;
  void SetEventCallback(const EventCallbackFn& callback) override { m_Callback = callback; }
  void SetVSync(bool) override {} // Browser presentation is controlled by requestAnimationFrame.
  bool IsVSync() const override { return true; }
  double GetTimeSeconds() const override;
  void CancelCloseRequest() override;
  void* GetNativeWindow() const override { return m_Window; }
  void Resize(uint32_t width, uint32_t height);
private:
  GLFWwindow* m_Window = nullptr;
  uint32_t m_Width = 0, m_Height = 0;
  EventCallbackFn m_Callback;
};
}
