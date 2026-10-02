#include "tcpch.h"
#include "tcpch.h"
#ifdef __EMSCRIPTEN__
#include "WebWindow.h"
#include "TomCat/Core/Input.h"
#include "TomCat/Events/ApplicationEvent.h"
#include <GLFW/glfw3.h>
#include <emscripten/html5.h>
#include <stdexcept>

namespace TomCat {
WebWindow::WebWindow(const WindowProps& props) : m_Width(props.Width), m_Height(props.Height) {
  if (!glfwInit()) throw std::runtime_error("GLFW browser initialization failed");
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  m_Window = glfwCreateWindow(m_Width, m_Height, props.Title.c_str(), nullptr, nullptr);
  if (!m_Window) { glfwTerminate(); throw std::runtime_error("WebGL2 context creation failed"); }
  glfwMakeContextCurrent(m_Window);
  glfwSetWindowUserPointer(m_Window, this);
  glfwSetKeyCallback(m_Window, [](GLFWwindow*, int key, int, int action, int) {
    if (key < 0) return;
    Input::NotifyKey(key, action == GLFW_RELEASE ? InputEventQueue::Action::Released :
      action == GLFW_REPEAT ? InputEventQueue::Action::Repeated : InputEventQueue::Action::Pressed, glfwGetTime());
  });
  glfwSetMouseButtonCallback(m_Window, [](GLFWwindow*, int button, int action, int) {
    Input::NotifyMouseButton(button, action == GLFW_RELEASE ? InputEventQueue::Action::Released : InputEventQueue::Action::Pressed, glfwGetTime());
  });
  glfwSetCursorPosCallback(m_Window, [](GLFWwindow*, double x, double y) { Input::NotifyMousePosition(x, y); });
  glfwSetScrollCallback(m_Window, [](GLFWwindow*, double x, double y) { Input::NotifyScroll(x, y); });
  glfwSetCharCallback(m_Window, [](GLFWwindow*, unsigned int codepoint) { Input::NotifyCharacter(codepoint); });
  glfwSetWindowFocusCallback(m_Window, [](GLFWwindow*, int focused) { Input::NotifyWindowFocus(focused != 0, glfwGetTime()); });
  glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
    auto& self = *static_cast<WebWindow*>(glfwGetWindowUserPointer(window));
    self.m_Width = width; self.m_Height = height;
    // The browser can resize the canvas box without the device pixel ratio changing, and
    // zooming changes the ratio at a constant CSS size. Reading both back from GLFW in one
    // place keeps the reported metrics consistent for either case.
    self.RefreshMetrics(true);
  });
  // Browser zoom and monitor changes fire the content scale callback. The interface has to
  // be rebaked at the new ratio, so report it as a metrics change even when the CSS size
  // stayed the same.
  glfwSetWindowContentScaleCallback(m_Window, [](GLFWwindow* window, float xScale, float yScale) {
    auto& self = *static_cast<WebWindow*>(glfwGetWindowUserPointer(window));
    const float scale = std::isfinite(xScale) && xScale > 0.0f ? xScale : 1.0f;
    const float scaleY = std::isfinite(yScale) && yScale > 0.0f ? yScale : 1.0f;
    const float next = (scale + scaleY) * 0.5f;
    if (std::abs(next - self.m_ContentScale) < 0.001f) return;
    self.m_ContentScale = next;
    self.RefreshMetrics(true);
  });
  RefreshMetrics(false);
}
WebWindow::~WebWindow() { if (m_Window) glfwDestroyWindow(m_Window); glfwTerminate(); Input::ClearState(); }
void WebWindow::PollEvents() { glfwPollEvents(); }
void WebWindow::Present() { glfwSwapBuffers(m_Window); }
void WebWindow::SetTitle(const std::string& title) { glfwSetWindowTitle(m_Window, title.c_str()); }
double WebWindow::GetTimeSeconds() const { return glfwGetTime(); }
void WebWindow::CancelCloseRequest() { glfwSetWindowShouldClose(m_Window, GLFW_FALSE); }
void WebWindow::Resize(uint32_t width, uint32_t height) {
  glfwSetWindowSize(m_Window, width, height);
  m_Width = width; m_Height = height;
  // glfwSetWindowSize drives the canvas resize listener, so the metrics are read back
  // afterwards instead of assuming the requested size became the buffer extent.
  RefreshMetrics(true);
}
void WebWindow::RefreshMetrics(bool dispatchEvent) {
  if (!m_Window) return;
  int windowWidth = 0, windowHeight = 0;
  int framebufferWidth = 0, framebufferHeight = 0;
  glfwGetWindowSize(m_Window, &windowWidth, &windowHeight);
  glfwGetFramebufferSize(m_Window, &framebufferWidth, &framebufferHeight);
  if (windowWidth > 0 && windowHeight > 0) { m_Width = uint32_t(windowWidth); m_Height = uint32_t(windowHeight); }
  if (!dispatchEvent || !m_Callback) return;
  // Content scale is the UI scale: the ratio between the CSS box and the drawing buffer can
  // legitimately differ from it, so the two are reported separately. The event is a named
  // local because the callback takes a non-const Event reference.
  WindowResizeEvent event(WindowMetrics::FromNative(static_cast<int>(m_Width),
    static_cast<int>(m_Height), framebufferWidth, framebufferHeight, m_ContentScale, m_ContentScale));
  m_Callback(event);
}
uint32_t WebWindow::GetFramebufferWidth() const {
  int width = 0, height = 0;
  if (m_Window) glfwGetFramebufferSize(m_Window, &width, &height);
  return width > 0 ? static_cast<uint32_t>(width) : m_Width;
}
uint32_t WebWindow::GetFramebufferHeight() const {
  int width = 0, height = 0;
  if (m_Window) glfwGetFramebufferSize(m_Window, &width, &height);
  return height > 0 ? static_cast<uint32_t>(height) : m_Height;
}
}
#endif // __EMSCRIPTEN__
